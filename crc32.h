#ifndef CRC32_H
#define CRC32_H

#include <QMainWindow>

uint crc32(QByteArray *src, uint len, uint state);

#endif
