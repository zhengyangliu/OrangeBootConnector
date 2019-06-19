#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <qdebug.h>
#include <QMessageBox>
#include <QTime>
#include <stdio.h>
#include <QFileInfo>
#include "crc32.h"

/* Defination ------------------------------------------------------------------------------------*/
/**
* @breif 基础参数
**/
#define BL_PROTOCOL_VERSION 		"0.1.1.0"     	/*!< 当前协议版本 */

#define PROTO_INSYNC				0xA5            /*!< Magic code of INSYNC */
#define PROTO_EOC					0xF7            /*!< Magic code of EOC */
#define PROTO_PROG_MULTI_MAX        64	            /*!< 最大单次烧写数据长度,单位:byte */
#define PROTO_REPLY_MAX             255	            /*!< 最大返回数据长度,单位:byte */

/**
* @breif 返回状态
**/
#define PROTO_OK					0x10            /*!< 操作成功 */
#define PROTO_FAILED				0x11            /*!< 操作失败 */
#define PROTO_INVALID				0x13	        /*!< 指令无效 */

/**
* @breif 操作指令
**/
#define PROTO_GET_SYNC				0x21            /*!< 测试同步 */

#define PROTO_GET_UDID				0x31            /*!< 读取芯片指定地址上的 UDID 12字节的值 */
#define PROTO_GET_FW_SIZE           0x32            /*!< 获取固件区大小 */

#define PROTO_GET_BL_REV            0x41            /*!< 获取Bootloader版本 */
#define PROTO_GET_ID                0x42            /*!< 获取电路板型号,包含版本 */
#define PROTO_GET_SN                0x43            /*!< 获取电路板序列号 */
#define PROTO_GET_REV               0x44            /*!< 获取电路板版本 */
#define PROTO_GET_FLASH_STRC        0x45            /*!< 获取FLASH结构描述 */
#define PROTO_GET_DES               0x46            /*!< 获取以 ASCII 格式读取设备描述 */

#define PROTO_CHIP_ERASE			0x51            /*!< 擦除设备 Flash 并复位编程指针 */
#define PROTO_PROG_MULTI			0x52            /*!< 在当前编程指针位置写入指定字节的数据，并使编程指针向后移动到下一段的位置 */
#define PROTO_GET_CRC				0x53	        /*!< 计算并返回CRC校验值 */
#define PROTO_BOOT					0x54            /*!< 引导 APP 程序 */


#define SerialPortBufferSize        2048            /*!< 串口缓存大小，单位字节 */

#define MAX_ERASE_TIME              1000            /*!< 最长擦除等待时间, 单位 10ms*/

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    /* 表格控件初始化设置 */
    model = new QStandardItemModel();
    ui->tableView->setModel(model);                            /* 绑定数据模型 */
    model->setColumnCount(7);
    model->setHeaderData(0, Qt::Horizontal, tr("Sector Num"));
    model->setHeaderData(1, Qt::Horizontal, tr("Start Address"));
    model->setHeaderData(2, Qt::Horizontal, tr("End Address"));
    model->setHeaderData(3, Qt::Horizontal, tr("Size"));
    model->setHeaderData(4, Qt::Horizontal, tr("Readable"));
    model->setHeaderData(5, Qt::Horizontal, tr("Writeable"));
    model->setHeaderData(6, Qt::Horizontal, tr("Erasable"));

    ui->tableView->setColumnWidth(0, 100);                                        /* 设定行宽 */
    ui->tableView->setColumnWidth(1, 100);
    ui->tableView->setColumnWidth(2, 100);
    ui->tableView->setColumnWidth(3, 100);
    ui->tableView->setColumnWidth(4, 100);
    ui->tableView->setColumnWidth(5, 80);
    ui->tableView->setColumnWidth(6, 80);
    ui->tableView->setColumnWidth(7, 80);
    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);  /* 固定行宽 */
    ui->tableView->verticalHeader()->setDefaultSectionSize(20);                   /* 设定行高 */
    ui->tableView->verticalHeader()->hide();                                      /* 关闭列头显示 */
    ui->tableView->horizontalHeader()->setStretchLastSection(true);               /* 列末尾对齐 */
    ui->tableView->setEditTriggers ( QAbstractItemView::NoEditTriggers );         /* 禁止调整宽度 */
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);           /* 选择时选中一行 */
    ui->tableView->setSelectionMode ( QAbstractItemView::SingleSelection);        /* 只能同时选择遗憾数据 */

    /* 连接设备前禁止功能按键 */
    ui->pushButton_2->setEnabled(false);
    ui->pushButton_3->setEnabled(false);
    ui->pushButton_5->setEnabled(false);

    /* 串口设定 */
    serial = new QSerialPort();                        /* 新建串口类 */
    serial->setReadBufferSize(SerialPortBufferSize);   /* 设置串口缓冲区大小 */
    scan_serial_port();                                /* 开启软件后直接执行一次串口扫描操作 */

    rx_data.resize(SerialPortBufferSize);              /* 串口消息处理接收缓存 */

    /* 由于comboBox没有鼠标点击的slot，所以使用事件过滤器来对点击操作进行响应 */
    ui->comboBox->installEventFilter(this);

    /* comboBox2 加入数据 */
    ui->comboBox_2->addItem("Auto");
    ui->comboBox_2->addItem("256000");
    ui->comboBox_2->addItem("115200");
    ui->comboBox_2->addItem("57600");
    ui->comboBox_2->addItem("38400");
    ui->comboBox_2->addItem("19200");
    ui->comboBox_2->addItem("14400");
    ui->comboBox_2->addItem("9600");

    baudrate_list[0] = 256000;
    baudrate_list[1] = 115200;
    baudrate_list[2] = 57600;
    baudrate_list[3] = 38400;
    baudrate_list[4] = 19200;
    baudrate_list[5] = 14400;
    baudrate_list[6] = 9600;

    /* 进度条归零 */
    ui->progressBar->setValue(0);

    /* 如果配置文件存在，则加载固件路径 */
    QFileInfo file(QCoreApplication::applicationDirPath() + "/config.ini");
    if(file.exists() == true)
    {
        QSettings *pIni = new QSettings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
        QString file_path_log = pIni->value("/Log/FilePath").toString();
        delete pIni;

        ui->textEdit->setText(file_path_log);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

/**
 * @brief 选择固件按钮点击事件
*/
void MainWindow::on_pushButton_clicked()
{
    QFileDialog *fileDialog = new QFileDialog(this);
    fileDialog->setWindowTitle(tr("选择固件"));
    fileDialog->setDirectory(".");
    fileDialog->setNameFilter(tr("Bin(*.bin)"));
    fileDialog->setFileMode(QFileDialog::ExistingFiles);
    fileDialog->setViewMode(QFileDialog::Detail);

    if(fileDialog->exec() == QDialog::Accepted)
    {
        QString fileNames = fileDialog->selectedFiles()[0];
        if(!fileNames.isEmpty())
        {
            ui->textEdit->setText(fileNames);
            qDebug()<<fileNames<<endl;

            // 建立ini配置文件记录bin固件
            QSettings *configIniWrite = new QSettings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
            configIniWrite->setValue("/Log/FilePath", fileNames);
            delete configIniWrite;
        }
    }
}

/**
 * @brief 对鼠标点击comboBox事件的处理
*/
bool MainWindow::eventFilter(QObject *f_object, QEvent *f_event)
{
    /* 鼠标点击comboBox事件. 操作：刷新串口列表 */
    if(f_object == ui->comboBox)
    {
        if(f_event->type() == QEvent::MouseButtonPress)
        {
            scan_serial_port();
        }
        return false;
    }
    return false;
}

/**
 * @brief 扫描可使用的串口并添加到combox中
 */
void MainWindow::scan_serial_port(void)
{
    QStringList new_portlist;
    QStringList current_portlist;

    /* 该函数会根据系统串口数量循环，每次检出一个串口信息 */
    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
        QSerialPort serial;
        serial.setPort(info);
        new_portlist << serial.portName();
    }

    /* 读取当前combox内容 */
    for(int i = 0; i < ui->comboBox->count(); i++)
    {
        current_portlist += ui->comboBox->itemText(i);
    }

    /* 添加新出现的串口号 */
    for(int i = 0; i < new_portlist.size(); i++)
    {
        /* 如果当前combox中不包含有new_portlist中的内容则 */
        if(!current_portlist.contains(new_portlist.at(i)))
        {
            ui->comboBox->addItem(new_portlist.at(i));
        }
    }

    /* 删除消失的串口号 */
    for(int i = 0; i < current_portlist.size(); i++)
    {
        /* 如果new_portlist中不包含有当前combox中的内容则 */
        if(!new_portlist.contains(current_portlist.at(i)))
        {
            ui->comboBox->removeItem(i);
        }
    }
}

/**
 * @brief 连接/断开设备按钮
 */
void MainWindow::on_pushButton_7_clicked()
{
    if(ui->pushButton_7->text() == tr("连接设备"))
    {
        if(connect_device() == 1)
        {
            ui->pushButton_7->setText("断开连接");
            ui->pushButton_2->setEnabled(true);
            ui->pushButton_3->setEnabled(true);
            ui->pushButton_5->setEnabled(true);

            qDebug()<<"串口已开启";
        }
    }
    else
    {
        serial->close();

        ui->pushButton_7->setText("连接设备");
        ui->pushButton_2->setEnabled(false);
        ui->pushButton_3->setEnabled(false);
        ui->pushButton_5->setEnabled(false);

        ui->textEdit_2->setText("");
        ui->textEdit_3->setText("");
        ui->textEdit_4->setText("");
        ui->textEdit_5->setText("");
        ui->textEdit_6->setText("");
        ui->textEdit_7->setText("");
        ui->textEdit_8->setText("");
        model->removeRows(0,model->rowCount());

        qDebug()<<"串口已关闭";
    }
}



/**
 * @brief 擦除APP按键点击事件
*/
void MainWindow::on_pushButton_2_clicked()
{
    // 锁定按键
    ui->pushButton_2->setEnabled(false);
    ui->pushButton_3->setEnabled(false);
    ui->pushButton_5->setEnabled(false);
    ui->pushButton_7->setEnabled(false);

    device_erase();

    // 解锁按键
    ui->pushButton_2->setEnabled(true);
    ui->pushButton_3->setEnabled(true);
    ui->pushButton_5->setEnabled(true);
    ui->pushButton_7->setEnabled(true);

}


void MainWindow::wait_ms(int s)
{
    QTime t;
    t.start();
    while(t.elapsed() < s)
        QCoreApplication::processEvents();
}

/**
* @brief  判断操作是否成功
* @param  [in] message_box bool. 0,不接受弹窗,1,接收弹窗报错
* @param  [out] data QByteArray. 接收到的数据
* @return 0,未收到指令.1,正确.2,无效.3,失败
*/
int MainWindow::rev_uart(bool message_box)
{
    int len = serial->bytesAvailable();
    rx_data = serial->readAll();
    if(len > 0)
    {
        if ((rx_data[len - 2].operator ==(PROTO_INSYNC)) && (rx_data[len - 1].operator ==(PROTO_OK)))
        {
            rx_len = len - 2;
            return 1;
        }
        else if ((rx_data[len - 2].operator ==(PROTO_INSYNC)) && (rx_data[len - 1].operator ==(PROTO_INVALID)))
        {
            if(message_box)
                QMessageBox::critical(this, "错误提示", "指令无效", QMessageBox::Ok);
            return 2;
        }
        else if ((rx_data[len - 2].operator ==(PROTO_INSYNC)) && (rx_data[len - 1].operator ==(PROTO_FAILED)))
        {
            if(message_box)
                QMessageBox::critical(this, "错误提示", "操作失败", QMessageBox::Ok);
            return 3;
        }
    }

    if(message_box)
        QMessageBox::critical(this, "错误提示", "操作超时", QMessageBox::Ok);

    return 0;
}

/**
* @brief  探测通信波特率
* @return 波特率
*/
int MainWindow::detect_device_baudrate(void)
{
    for(int i = 0; i < BaudRate_Num; i++)
    {
        qDebug()<<"try"<<baudrate_list[i];
        serial->setBaudRate(baudrate_list[i]);

        if(send_normal_cmd(PROTO_GET_SYNC, NULL, 50, 0) == 1)
        {
            qDebug()<<"found baudrate"<<baudrate_list[i];
            return baudrate_list[i];
        }
    }

    qDebug()<<"baudrate not found !"<<BaudRate_Num;
    return 0;
}

bool MainWindow::connect_device(void)
{
    /* 如果串口列表选择非空 */
    if(!ui->comboBox->currentText().isEmpty())
    {
        /* 设定串口参数 */
        serial->setPortName(ui->comboBox->currentText());
        serial->setDataBits(QSerialPort::Data8);
        serial->setParity(QSerialPort::NoParity);
        serial->setStopBits(QSerialPort::OneStop);
        serial->setFlowControl(QSerialPort::NoFlowControl);

        /* 尝试开启串口 */
        if(serial->open(QIODevice::ReadWrite) != true)
        {
            QMessageBox::critical(this, "错误提示", "该串口不存在或已被占用", QMessageBox::Ok);
            return 0;
        }
        else
        {
            /* 设定波特率 */
            if(ui->comboBox_2->currentText() == "Auto")
            {
                int rev = detect_device_baudrate();
                if(rev == 0)
                {
                    serial->close();
                    QMessageBox::critical(this, "错误提示", "该未发现合适的串口频率,请确认设备是否正确连接并运行", QMessageBox::Ok);
                    return 0;
                }
                else
                    serial->setBaudRate(rev);
            }
            else
                serial->setBaudRate(ui->comboBox_2->currentText().toInt());

            /* 尝试同步设备 */
            if (send_normal_cmd(PROTO_GET_SYNC, NULL, 50, 0) == 1)
            {
                get_device_udid();
                get_device_fw_size();
                get_device_bl_rev();
                get_device_id();
                get_device_sn();
                get_device_rev();
                get_device_des();
                get_device_fl_strc();
                return 1;
            }
            else
            {
                serial->close();
                QMessageBox::critical(this, "错误提示", "同步失败", QMessageBox::Ok);
                return 0;
            }
        }
    }

    return 0;
}

/**
 * @brief 烧写固件按钮点击事件
*/
void MainWindow::on_pushButton_3_clicked()
{

    ui->pushButton_2->setEnabled(false);
    ui->pushButton_3->setEnabled(false);
    ui->pushButton_5->setEnabled(false);
    ui->pushButton_7->setEnabled(false);

    QFile file(ui->textEdit->toPlainText());

    if(!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, "错误提示", file.errorString(), QMessageBox::Ok);
        ui->pushButton_2->setEnabled(true);
        ui->pushButton_3->setEnabled(true);
        ui->pushButton_5->setEnabled(true);
        ui->pushButton_7->setEnabled(true);
        return;
    }
    else
    {
        QByteArray data=file.readAll();//读取文件
        file.close();

        long filelen = data.size();
        qDebug() << "文件载入成功. 大小" << filelen << "字节";

        if(filelen % 4 != 0)
        {
            QMessageBox::critical(this, "错误提示", "文件非法，长度不符合4字节的倍数", QMessageBox::Ok);
            ui->pushButton_2->setEnabled(true);
            ui->pushButton_3->setEnabled(true);
            ui->pushButton_5->setEnabled(true);
            ui->pushButton_7->setEnabled(true);
            return;
        }
        if(filelen > fw_size)
        {
            QMessageBox::critical(this, "错误提示", "文件超过固件区大小", QMessageBox::Ok);
            ui->pushButton_2->setEnabled(true);
            ui->pushButton_3->setEnabled(true);
            ui->pushButton_5->setEnabled(true);
            ui->pushButton_7->setEnabled(true);
            return;
        }

        if(device_erase() == 0)
            return;

        bool fit = 0;
        long divide = filelen / ((PROTO_PROG_MULTI_MAX -1) * 4);     // 计算分割数
        if (filelen - (divide * ((PROTO_PROG_MULTI_MAX -1)) * 4) > 0)  // 判断文件长度是否为 252字节 的整数
        {
            divide += 1;
            fit = 1;
        }

        //设置进度条大小
        ui->progressBar->setRange(0,divide);
        qDebug() << "divide = " << divide;
        ui->progressBar->setValue(0);


        QByteArray tx_data;

        for (int i = 0; i < divide; i++)
        {
            int package_len = 0;

            tx_data.resize(1);
            tx_data[0] = PROTO_PROG_MULTI;

            if ((i == (divide - 1)) && (fit == 1))
                package_len = filelen - ((divide - 1) * (PROTO_PROG_MULTI_MAX -1) * 4);
            else
                package_len = (PROTO_PROG_MULTI_MAX -1) * 4;

            tx_data.append(1, package_len);

            for (int j = 0; j < package_len; j++)
            {
                tx_data.append(1, data.at(i * (PROTO_PROG_MULTI_MAX -1) * 4 + j));
            }

            tx_data.append(1,PROTO_EOC);  //结尾

            serial->clear(QSerialPort::Input);
            serial->write(tx_data);

            int k = 0;
            for(; k < 1000; k++) // 1s
            {
                wait_ms(1);

                if (rev_uart(0) == 1)
                {
                    ui->progressBar->setValue(ui->progressBar->value() + 1);
                    qDebug() << "finish = " << i;
                    break;
                }
            }

            if(k == 1000)
            {
                QMessageBox::critical(this, "错误提示", "操作超时", QMessageBox::Ok);
                return;
            }
        }

        ui->progressBar->setValue(100);
        qDebug() << "flash ok";

        /*
         * CRC校验
        */
        uint sum = 0;
        QByteArray fill_data;
        fill_data.resize(1);
        fill_data[0] = 0xff;

        sum = crc32(&data, filelen, sum);

        for (long tmp = 0; tmp < (fw_size - filelen); tmp++) // 填充剩余字节
        {
            sum = crc32(&fill_data, 1, sum);
        }

        device_crc(sum);

        ui->pushButton_2->setEnabled(true);
        ui->pushButton_3->setEnabled(true);
        ui->pushButton_5->setEnabled(true);
        ui->pushButton_7->setEnabled(true);
        ui->progressBar->setValue(500);
    }
}

void MainWindow::on_pushButton_5_clicked()
{
    if(device_boot())
    {
        serial->close();

        ui->pushButton_7->setText("连接设备");
        ui->pushButton_2->setEnabled(false);
        ui->pushButton_3->setEnabled(false);
        ui->pushButton_5->setEnabled(false);

        ui->textEdit_2->setText("");
        ui->textEdit_3->setText("");
        ui->textEdit_4->setText("");
        ui->textEdit_5->setText("");
        ui->textEdit_6->setText("");
        ui->textEdit_7->setText("");
        ui->textEdit_8->setText("");
        model->removeRows(0,model->rowCount());
    }
}

int MainWindow::get_rxdata(QByteArray* data)
{
    QByteArray buffer;
    uint len = serial->bytesAvailable();

    if(len == 0)     /* 未收到数据 */
        return 0;

    else             /* 收到数据 */
    {
        buffer = serial->readAll();   /* 读取串口缓存内的数据 */

        if ((buffer[len - 2].operator ==(PROTO_INSYNC)) && (buffer[len - 1].operator ==(PROTO_OK)))             /* 操作成功 */
        {
            if(data != NULL)
                *data = buffer.mid(0, len - 2);
            return 1;
        }
        else if ((buffer[len - 2].operator ==(PROTO_INSYNC)) && (buffer[len - 1].operator ==(PROTO_INVALID)))   /* 操作无效 */
        {
            return 2;
        }
        else if ((buffer[len - 2].operator ==(PROTO_INSYNC)) && (buffer[len - 1].operator ==(PROTO_FAILED)))    /* 操作失败 */
        {
            return 3;
        }
    }

    return 0;
}

int MainWindow::send_normal_cmd(int cmd, QByteArray* rx, int timeout, bool msg_box)
{
    QByteArray tx_data;
    int _i;

    tx_data.resize(2);
    tx_data[0] = cmd;
    tx_data[1] = PROTO_EOC;

    serial->clear(QSerialPort::Input);
    serial->write(tx_data);

    wait_ms(timeout);

    _i = get_rxdata(rx);

    if(msg_box == true)
    {
        switch (_i) {
        case 0:
            QMessageBox::critical(this, "错误提示", "操作超时", QMessageBox::Ok);
            break;
        case 2:
            QMessageBox::critical(this, "错误提示", "指令无效", QMessageBox::Ok);
            break;
        case 3:
            QMessageBox::critical(this, "错误提示", "操作失败", QMessageBox::Ok);
            break;
        default:
            break;
        }
    }

    return _i;
}

bool MainWindow::get_device_udid(void)
{
    QByteArray rx_buf;
    if(send_normal_cmd(PROTO_GET_UDID, &rx_buf, 20, 1) == 1)
    {
        QByteArray revert;
        revert.resize(12);

        for(int i = 0; i < 12; i++)
            revert[i] = rx_buf[12 - i];

        ui->textEdit_2->setText(revert.mid(0,12).toHex().toUpper());
        return 1;
    }
    return 0;
}

bool MainWindow::get_device_fw_size(void)
{
    QByteArray rx_buf;
    if(send_normal_cmd(PROTO_GET_FW_SIZE, &rx_buf, 20, 1) == 1)
    {
        uint tmp = rx_buf[0] & 0xff;
        tmp += (rx_buf[1] & 0xff) * 256;
        tmp += (rx_buf[2] & 0xff) * 65536;
        tmp += (rx_buf[3] & 0xff) * 16777216;
        fw_size = tmp;

        ui->textEdit_3->setText(QString::number(tmp / 1024, 10) + "KB");
        return 1;
    }
    return 0;
}

bool MainWindow::get_device_bl_rev(void)
{
    QByteArray rx_buf;
    if(send_normal_cmd(PROTO_GET_BL_REV, &rx_buf, 20, 1) == 1)
    {
        ui->textEdit_4->setText(rx_buf);
        return 1;
    }
    return 0;
}

bool MainWindow::get_device_id(void)
{
    QByteArray rx_buf;
    if(send_normal_cmd(PROTO_GET_ID, &rx_buf, 20, 1) == 1)
    {
        ui->textEdit_5->setText(QString::fromLocal8Bit(rx_buf));
        return 1;
    }
    return 0;
}

bool MainWindow::get_device_sn(void)
{
    QByteArray rx_buf;
    if(send_normal_cmd(PROTO_GET_SN, &rx_buf, 20, 1) == 1)
    {
        ui->textEdit_6->setText(rx_buf);
        return 1;
    }
    return 0;
}

bool MainWindow::get_device_rev(void)
{
    QByteArray rx_buf;
    if(send_normal_cmd(PROTO_GET_REV, &rx_buf, 20, 1) == 1)
    {
        ui->textEdit_7->setText(rx_buf);
        return 1;
    }
    return 0;
}

bool MainWindow::get_device_des(void)
{
    QByteArray rx_buf;
    if(send_normal_cmd(PROTO_GET_DES, &rx_buf, 100, 1) == 1)
    {
        ui->textEdit_8->setText(rx_buf);
        return 1;
    }
    return 0;
}

bool MainWindow::get_device_fl_strc(void)
{
    QByteArray rx_buf;
    if(send_normal_cmd(PROTO_GET_FLASH_STRC, &rx_buf, 100, 1) == 1)
    {
        fl_strc_to_table(rx_buf);
        return 1;
    }
    return 0;
}

void MainWindow::fl_strc_to_table(QString text)
{
    qDebug() << text << endl;

    long num = 0;

    // 分割存储器
    QStringList storge = text.split(QRegExp("@"));

    for(int i = 1; i < storge.count() ; i++)
    {
        // 分割储存器描述
        QString tmp1 = storge.at(i);
        QStringList des = tmp1.split(QRegExp("/"));

        // 储存器位置
        QString position = des.at(0);

        // 储存器起始地址
        QString tmp = des.at(1);
        long addr = tmp.mid(2,tmp.count()).toLong(NULL,16);

        // 分割sector
        tmp1 = des.at(2);
        QStringList sector = tmp1.split(QRegExp(","));

        for(int j = 0; j < sector.count(); j++)
        {
            // 分割大小和数量
            QString tmp2 = sector.at(j);

            qDebug() << tmp2.section("*", 0, 0) << endl;
            qDebug() << tmp2.section("*", 1, 1) << endl;

            for(int k = 0; k < tmp2.section("*", 0, 0).toInt(); k++)
            {
                // Sector Num
                model->setItem(num, 0, new QStandardItem(QString::number(num, 10)));

                // Start Addr
                model->setItem(num, 1, new QStandardItem("0x" + QString("%1").arg(addr, 8, 16, QChar('0'))));


                QString tmp3 = tmp2.section("*", 1, 1);;
                long size = tmp3.mid(0, (tmp3.count() - 2)).toLong();
                addr += size * 1024;

                // End Addr
                model->setItem(num, 2, new QStandardItem("0x" + QString("%1").arg(addr, 8, 16, QChar('0'))));

                // Size
                model->setItem(num, 3, new QStandardItem(QString::number(size, 10).toUpper() + " kb"));

                QString rwb = tmp3.mid(tmp3.count() - 1);

                if(rwb == tr("a"))
                {
                    model->setItem(num, 4, new QStandardItem("X"));
                    model->item(num,4)->setTextAlignment(Qt::AlignCenter);
                }
                if(rwb == tr("b"))
                {
                    model->setItem(num, 6, new QStandardItem("X"));
                    model->item(num,6)->setTextAlignment(Qt::AlignCenter);
                }
                if(rwb == tr("c"))
                {
                    model->setItem(num, 4, new QStandardItem("X"));
                    model->item(num,4)->setTextAlignment(Qt::AlignCenter);
                    model->setItem(num, 6, new QStandardItem("X"));
                    model->item(num,6)->setTextAlignment(Qt::AlignCenter);
                }
                if(rwb == tr("d"))
                {
                    model->setItem(num, 5, new QStandardItem("X"));
                    model->item(num,5)->setTextAlignment(Qt::AlignCenter);
                }
                if(rwb == tr("e"))
                {
                    model->setItem(num, 4, new QStandardItem("X"));
                    model->item(num,4)->setTextAlignment(Qt::AlignCenter);
                    model->setItem(num, 5, new QStandardItem("X"));
                    model->item(num,5)->setTextAlignment(Qt::AlignCenter);
                }
                if(rwb == tr("f"))
                {
                    model->setItem(num, 5, new QStandardItem("X"));
                    model->item(num,5)->setTextAlignment(Qt::AlignCenter);
                    model->setItem(num, 6, new QStandardItem("X"));
                    model->item(num,6)->setTextAlignment(Qt::AlignCenter);
                }
                if(rwb == tr("g"))
                {
                    model->setItem(num, 4, new QStandardItem("X"));
                    model->item(num,4)->setTextAlignment(Qt::AlignCenter);
                    model->setItem(num, 5, new QStandardItem("X"));
                    model->item(num,5)->setTextAlignment(Qt::AlignCenter);
                    model->setItem(num, 6, new QStandardItem("X"));
                    model->item(num,6)->setTextAlignment(Qt::AlignCenter);
                }

                num++;
            }
        }
    }
}

bool MainWindow::device_erase(void)
{

    ui->progressBar->setRange(0,MAX_ERASE_TIME);
    ui->progressBar->setValue(0);

    if(send_normal_cmd(PROTO_CHIP_ERASE, NULL, 50, 0) == 1)
    {
        ui->progressBar->setValue(MAX_ERASE_TIME);
        return 1;
    }

    /**
     * @note 10ms查询一次缓存数据，轮询MAX_ERASE_TIME次，即 MAX_ERASE_TIME * 10ms 后无反应视为超时
    */
    int i = 0;
    for(; i < MAX_ERASE_TIME; i++)
    {
        wait_ms(10);
        ui->progressBar->setValue(ui->progressBar->value() + 1);

        if (get_rxdata(NULL) == 1)
        {
            qDebug() << "erase ok. i = " << i;
            ui->progressBar->setValue(MAX_ERASE_TIME);
            return 1;
        }
    }

    QMessageBox::critical(this, "错误提示", "操作超时", QMessageBox::Ok);
    return 0;
}

bool MainWindow::device_boot(void)
{
    if(send_normal_cmd(PROTO_BOOT, NULL, 50, 1) == 1)
        return 1;
    return 0;
}

bool MainWindow::device_crc(uint sum)
{
    QByteArray rx_tmp;

    ui->progressBar->setValue(0);
    ui->progressBar->setRange(0,500);

    if(send_normal_cmd(PROTO_GET_CRC, &rx_tmp, 50, 0) == 1)
    {
        uint crc;

        crc = rx_tmp[0] & 0xff;
        crc += (rx_tmp[1] & 0xff) * 256;
        crc += (rx_tmp[2] & 0xff) * 65536;
        crc += (rx_tmp[3] & 0xff) * 16777216;

        if(crc == sum)
        {
            qDebug() << "crc right";
            return 1;
        }
        else
        {
            QMessageBox::critical(this, "错误提示", "校验失败", QMessageBox::Ok);
            return 0;
        }
    }

    for(int i = 0; i < 500; i++)    // 5s
    {
        wait_ms(10);
        ui->progressBar->setValue(ui->progressBar->value() + 1);

        if (get_rxdata(&rx_tmp) == 1)
        {
            uint crc;

            crc = rx_tmp[0] & 0xff;
            crc += (rx_tmp[1] & 0xff) * 256;
            crc += (rx_tmp[2] & 0xff) * 65536;
            crc += (rx_tmp[3] & 0xff) * 16777216;

            if(crc == sum)
            {
                qDebug() << "crc right";
                return 1;
            }
            else
            {
                QMessageBox::critical(this, "错误提示", "校验失败", QMessageBox::Ok);
                return 0;
            }
        }
    }

    QMessageBox::critical(this, "错误提示", "操作超时", QMessageBox::Ok);
    return 0;
}






