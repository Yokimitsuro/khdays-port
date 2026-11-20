/* Little-endian integers read out of bytes (the game's files, a zip's records, the network's packets): one copy for
 * the port's C. */
#ifndef KHDAYS_BYTES_H
#define KHDAYS_BYTES_H

static inline unsigned le16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static inline unsigned le32(const unsigned char *p)
{
    return le16(p) | le16(p + 2) << 16;
}

#endif
