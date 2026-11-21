#ifndef USB_MSC_DISK_H
#define USB_MSC_DISK_H

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_partition.h>
#include <vector>

class UsbMscDisk {
public:
    bool begin(uint32_t diskSizeBytes = 1024 * 1024,
               uint32_t sectorSize = 512);

    uint8_t *data();
    uint32_t sectorCount() const;
    uint32_t bytesPerSector() const;

    bool loadPartition();
    bool readRootFile(const char *name,
                      std::vector<uint8_t> &data);

private:
    uint8_t *_disk = nullptr;
    uint32_t _diskSize = 0;
    uint32_t _sectorSize = 0;
    const esp_partition_t *_partition = nullptr;

    void formatFat12();
    void setFat12(uint32_t sector, uint16_t value);
};

extern UsbMscDisk USBMscDisk;

#endif
