#ifndef MINIOS_FDC_H
#define MINIOS_FDC_H

void fdc_on_irq(void);
void fdc_invalidate(void);
void fdc_set_hold(int hold);
void fdc_release_hold(void);
int fdc_init(void);
int fdc_read_sector(unsigned int lba, unsigned char *dst);
int fdc_write_sector(unsigned int lba, const unsigned char *src);

#endif
