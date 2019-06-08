#include "crc32.h"
#include <stdio.h>

uint crc32(QByteArray *src, uint len, uint state)
{
    static uint crctab[256];

	/* check whether we have generated the CRC table yet */
	/* this is much smaller than a static table */
	if (crctab[1] == 0)
	{
		for (unsigned int i = 0; i < 256; i++)
		{
            uint c = i;

			for (unsigned int j = 0; j < 8; j++)
			{
				if (c & 1)
				{
					c = 0xedb88320U ^ (c >> 1);

				}
				else
				{
					c = c >> 1;
				}
			}

			crctab[i] = c;
		}
	}

	for (unsigned int i = 0; i < len; i++)
	{
        state = crctab[(state ^ src->at(i)) & 0xff] ^ (state >> 8);
	}

	return state;
}
