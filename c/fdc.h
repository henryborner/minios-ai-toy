#ifndef MINIOS_FDC_H
#define MINIOS_FDC_H

void fdc_on_irq(void);
int fdc_init(void);
int fdc_read_sector(unsigned int lba, unsigned char *dst);
int fdc_write_sector(unsigned int lba, const unsigned char *src);

#endif
