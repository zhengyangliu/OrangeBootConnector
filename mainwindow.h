#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtSerialPort>
#include <QStandardItemModel>

#define BaudRate_Num                7

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_pushButton_clicked();
    void on_pushButton_7_clicked();
    void on_pushButton_2_clicked();
    void on_pushButton_3_clicked();
    void on_pushButton_5_clicked();

private:
    Ui::MainWindow *ui;
    QSerialPort *serial;
    int baudrate_list[BaudRate_Num];

    QByteArray rx_data;
    int rx_len;
    QStandardItemModel *model;
    long fw_size;

    void scan_serial_port(void);
    bool eventFilter(QObject *f_object, QEvent *f_event);

    int rev_uart(bool message_boxn);
    void wait_ms(int s);

    // TODO: 新的重构函数
    int get_rxdata(QByteArray* data);
    int send_normal_cmd(int cmd, QByteArray* rx, int timeout, bool msg_box);
    bool get_device_udid(void);
    bool get_device_fw_size(void);
    bool get_device_bl_rev(void);
    bool get_device_id(void);
    bool get_device_sn(void);
    bool get_device_rev(void);
    bool get_device_des(void);
    bool get_device_fl_strc(void);
    void fl_strc_to_table(QString text);
    bool device_erase(void);
    bool device_boot(void);
    bool device_program(QByteArray *data);
    bool device_crc(uint crc);

    int detect_device_baudrate(void);
    bool connect_device(void);
};

#endif // MAINWINDOW_H
