#include "bits.h"
#include "spi_pga.h"

/* The MAX9939 requires data to be sent LSB first (ignoring the
   contradictory diagram in the datasheet) but most SPI peripherals
   send data MSB first.  In this driver, the data is swapped around.
*/

enum 
{
    MAX9939_SHDN = BIT (0),
    MAX9939_MEAS = BIT (1),
    MAX9939_NEG = BIT (2),
    MAX9939_GAIN = BIT (7)
};


/* The minimum gain is 0.2 for Vcc = 5 V or 0.25 for Vcc = 3.3 V.
   Let's assume 3.3 V operation and scale all the gains by 4.  */
#define GAIN_SCALE(GAIN) ((GAIN) * 4)

static const spi_pga_gain_t max9939_gains[] =
{
    GAIN_SCALE (0.25),
    GAIN_SCALE (1),
    GAIN_SCALE (10),
    GAIN_SCALE (20),
    GAIN_SCALE (30),
    GAIN_SCALE (40),
    GAIN_SCALE (60),
    GAIN_SCALE (80),
    GAIN_SCALE (120),
    GAIN_SCALE (157),
    GAIN_SCALE (0)
};


#define GAIN_COMMAND(REGVAL) (((REGVAL) >> 1) | MAX9939_GAIN)

static const uint8_t gain_commands[] =
{
    GAIN_COMMAND (0x90),
    GAIN_COMMAND (0x00),
    GAIN_COMMAND (0x80),
    GAIN_COMMAND (0x40),
    GAIN_COMMAND (0xc0),
    GAIN_COMMAND (0x20),
    GAIN_COMMAND (0xa0),
    GAIN_COMMAND (0x60),
    GAIN_COMMAND (0xe0),
    GAIN_COMMAND (0x80)
};


typedef struct
{
    uint16_t offset;
    uint8_t regval;
} max9939_offset_map_t;


#define OFFSET_MAP(OFFSET, REGVAL) {.offset = (OFFSET) * 10, \
            .regval = ((REGVAL) << 3)}

static const max9939_offset_map_t offset_map[] =
{
    OFFSET_MAP (0.0, 0x0),
    OFFSET_MAP (1.3, 0x8),
    OFFSET_MAP (2.5, 0x4),
    OFFSET_MAP (3.8, 0xc),
    OFFSET_MAP (4.9, 0x2),
    OFFSET_MAP (6.1, 0xa),
    OFFSET_MAP (7.3, 0x6),
    OFFSET_MAP (8.4, 0xe),
    OFFSET_MAP (10.6, 0x1),
    OFFSET_MAP (11.7, 0x9),
    OFFSET_MAP (12.7, 0x5),
    OFFSET_MAP (13.7, 0xd),
    OFFSET_MAP (14.7, 0x3),
    OFFSET_MAP (15.7, 0xb),
    OFFSET_MAP (16.7, 0x7),
    OFFSET_MAP (17.6, 0xf)
};


static bool
max9939_gain_set (spi_pga_t pga, uint8_t gain_index)
{
    uint8_t command[1];
    
    command[0] = gain_commands[gain_index];
    
    return spi_pga_command (pga, command, ARRAY_SIZE (command));
}


static spi_pga_offset_t
max9939_offset_set1 (spi_pga_t pga, uint index, bool negative, bool measure)
{
    spi_pga_offset_t offset;
    uint8_t command[1];
    
    offset = offset_map[index].offset;
    
    command[0] = offset_map[index].regval;

    if (negative)
    {
        offset = -offset;
        command[0] |= MAX9939_NEG;
    }

    if (measure)
        command[0] |= MAX9939_MEAS;
    
    if (!spi_pga_command (pga, command, ARRAY_SIZE (command)))
        return 0;

    pga->offset = offset;
    return offset;
}


/* Setting a positive offset (in 0.1 mV steps) makes the output drop.  */
static spi_pga_offset_t
max9939_offset_set (spi_pga_t pga, spi_pga_offset_t offset, bool measure)
{
    unsigned int i;
    int16_t prev_offset;
    bool negative;

    /* Need to measure offset voltage at low(ish) gains otherwise will
       have saturation.  For example, the worst case correction is
       17.1 mV and with the maximum gain of 628 this produces 10 V of
       offset.  Thus the maximum gain to avoid saturation is 80.  Now
       it appears that the offset also varies with gain but this is
       probably a secondary effect.  */

    negative = offset < 0;
    if (negative)
        offset = -offset;

    /* Perhaps should search for closest value.  */
    prev_offset = 0;
    for (i = 0; i < ARRAY_SIZE (offset_map); i++)
    {
        if (offset >= prev_offset && offset < offset_map[i].offset)
            break;
        prev_offset = offset_map[i].offset;
    }
    
    return max9939_offset_set1 (pga, i - 1, negative, measure);
}


static bool 
max9939_shutdown_set (spi_pga_t pga, bool enable)
{
    uint8_t command[1];

    if (enable)
        command[0]= 0;
    else
        command[0] = MAX9939_SHDN;

    if (!spi_pga_command (pga, command, ARRAY_SIZE (command)))
                return 0;
    return 1;
}


spi_pga_ops_t max9939_ops =
{
    .gain_set = max9939_gain_set,   
    .channel_set = 0,
    .offset_set = max9939_offset_set,   
    .shutdown_set = max9939_shutdown_set,   
    .gains = max9939_gains
};


