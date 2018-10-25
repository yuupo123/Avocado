#include "tables.h"

namespace ADPCM {
int16_t zigzagTables[7][29]
    = {{0x0000, 0x0000, 0x0000,  0x0000, 0x0000,  -0x0002, 0x000A,  -0x0022, 0x0041, -0x0054, 0x0034, 0x0009,  -0x010A, 0x0400, -0x0A78,
        0x234C, 0x6794, -0x1780, 0x0BCD, -0x0623, 0x0350,  -0x016D, 0x006B,  0x000A, -0x0010, 0x0011, -0x0008, 0x0003,  -0x0001},
       {0x0000, 0x0000,  0x0000, -0x0002, 0x0000, 0x0003,  -0x0013, 0x003C,  -0x004B, 0x00A2, -0x00E3, 0x0132, -0x0043, -0x0267, 0x0C9D,
        0x74BB, -0x11B4, 0x09B8, -0x05BF, 0x0372, -0x01A8, 0x00A6,  -0x001B, 0x0005,  0x0006, -0x0008, 0x0003, -0x0001, 0x0000},
       {0x0000,  0x0000, -0x0001, 0x0003, -0x0002, -0x0005, 0x001F,  -0x004A, 0x00B3,  -0x0192, 0x02B1, -0x039E, 0x04F8, -0x05A6, 0x7939,
        -0x05A6, 0x04F8, -0x039E, 0x02B1, -0x0192, 0x00B3,  -0x004A, 0x001F,  -0x0005, -0x0002, 0x0003, -0x0001, 0x0000, 0x0000},
       {
           0x0000,  -0x0001, 0x0003, -0x0008, 0x0006, 0x0005,  -0x001B, 0x00A6,  -0x01A8, 0x0372, -0x05BF, 0x09B8, -0x11B4, 0x74BB, 0x0C9D,
           -0x0267, -0x0043, 0x0132, -0x00E3, 0x00A2, -0x004B, 0x003C,  -0x0013, 0x0003,  0x0000, -0x0002, 0x0000, 0x0000,  0x0000,
       },
       {0x0001, 0x0003,  -0x0008, 0x0011, -0x0010, 0x000A, 0x006B,  -0x016D, 0x0350,  -0x0623, 0x0BCD, -0x1780, 0x6794, 0x234C, -0x0A78,
        0x0400, -0x010A, 0x0009,  0x0034, -0x0054, 0x0041, -0x0022, 0x000A,  -0x0001, 0x0000,  0x0001, 0x0000,  0x0000, 0x0000},
       {
           0x0002,  -0x0008, 0x0010,  -0x0023, 0x002B, 0x001A,  -0x00EB, 0x027B,  -0x0548, 0x0AFA, -0x16FA, 0x53E0, 0x3C07, -0x1249, 0x080E,
           -0x0347, 0x015B,  -0x0044, -0x0017, 0x0046, -0x0023, 0x0011,  -0x0005, 0x0000,  0x0000, 0x0000,  0x0000, 0x0000, 0x0000,
       },
       {-0x0005, 0x0011,  -0x0023, 0x0046, -0x0017, -0x0044, 0x015B,  -0x0347, 0x080E, -0x1249, 0x3C07, 0x53E0, -0x16FA, 0x0AFA, -0x0548,
        0x027B,  -0x00EB, 0x001A,  0x002B, -0x0023, 0x0010,  -0x0008, 0x0002,  0x0000, 0x0000,  0x0000, 0x0000, 0x0000,  0x0000}};

};