#include "usb_msc_disk.h"

#include <esp_heap_caps.h>
#include <esp_partition.h>

USBMSC MSC;
UsbMscDisk USBMscDisk;

static uint8_t *mscDiskBuffer = nullptr;
static const esp_partition_t *fatDiskPartition = nullptr;
static uint8_t flashBlock[4096];

static int32_t mscRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    if (mscDiskBuffer == nullptr) {
        return -1;
    }

    uint32_t diskOffset = lba * 512 + offset;
    if (diskOffset + bufsize > fatDiskPartition->size) {
        return -1;
    }

    memcpy(buffer, mscDiskBuffer + diskOffset, bufsize);
    return bufsize;
}

static int32_t mscWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    if (mscDiskBuffer == nullptr || fatDiskPartition == nullptr || bufsize == 0) {
        return -1;
    }

    uint32_t diskOffset = lba * 512 + offset;
    if (diskOffset + bufsize > fatDiskPartition->size) {
        return -1;
    }

    memcpy(mscDiskBuffer + diskOffset, buffer, bufsize);

    uint32_t startBlock = diskOffset & ~4095U;
    uint32_t endBlock = (diskOffset + bufsize - 1) & ~4095U;

    for (uint32_t blockOffset = startBlock; blockOffset <= endBlock; blockOffset += 4096U) {
        memcpy(flashBlock, mscDiskBuffer + blockOffset, sizeof(flashBlock));

        if (esp_partition_erase_range(fatDiskPartition, blockOffset, sizeof(flashBlock)) != ESP_OK) {
            return -1;
        }
        if (esp_partition_write(fatDiskPartition, blockOffset,
                                flashBlock, sizeof(flashBlock)) != ESP_OK) {
            return -1;
        }
    }

    return bufsize;
}

static bool mscStartStop(uint8_t, bool, bool) {
    return true;
}

uint8_t *UsbMscDisk::data() {
    return _disk;
}

uint32_t UsbMscDisk::sectorCount() const {
    return _diskSize / _sectorSize;
}

uint32_t UsbMscDisk::bytesPerSector() const {
    return _sectorSize;
}

void UsbMscDisk::setFat12(uint32_t sector, uint16_t value) {
    uint32_t fatOffset = sector + (sector / 2);
    uint8_t *fat = _disk + 512;

    if (sector & 1) {
        fat[fatOffset] = (fat[fatOffset] & 0x0F) | ((value & 0x0F) << 4);
        fat[fatOffset + 1] = value >> 4;
    } else {
        fat[fatOffset] = value & 0xFF;
        fat[fatOffset + 1] = (fat[fatOffset + 1] & 0xF0) | (value >> 8);
    }
}

void UsbMscDisk::formatFat12() {
    memset(_disk, 0, _diskSize);

    uint8_t *boot = _disk;
    boot[0] = 0xEB;
    boot[1] = 0x3C;
    boot[2] = 0x90;
    memcpy(boot + 3, "MSDOS5.0", 8);

    boot[11] = 0x00;
    boot[12] = 0x02;                  // 512 bytes/sector
    boot[13] = 1;                     // sectors/cluster
    boot[14] = 1; boot[15] = 0;       // reserved sectors
    boot[16] = 2;                     // two FATs
    boot[17] = 64; boot[18] = 0;      // root entries
    boot[19] = 0x00; boot[20] = 0x08; // total 2048 sectors
    boot[21] = 0xF8;
    boot[22] = 6; boot[23] = 0;       // sectors per FAT
    boot[24] = 32; boot[25] = 0;
    boot[26] = 64; boot[27] = 0;
    boot[28] = 0; boot[29] = 0; boot[30] = 0; boot[31] = 0;
    boot[32] = 0; boot[33] = 0; boot[34] = 0; boot[35] = 0;
    boot[36] = 0x80;                  // physical drive
    boot[37] = 0x00;                  // reserved
    boot[38] = 0x29;                  // extended boot signature
    boot[39] = 0x32; boot[40] = 0x50; boot[41] = 0x0A; boot[42] = 0x00;
    memcpy(boot + 43, "RCM-JIG     ", 11);
    memcpy(boot + 54, "FAT12   ", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;

    setFat12(0, 0xFF8);
    setFat12(1, 0xFFF);
    setFat12(2, 0xFFF);

    memcpy(_disk + (7 * 512), _disk + 512, 6 * 512);

    uint8_t *root = _disk + (13 * 512);

    memcpy(root, "RCM-JIG     ", 11);
    root[11] = 0x08;

    static const char ReadmeText[] =
    "The Payload needs to be named payload.bin and the ESP32 needs to be ejected safely from the computer\r\n";
    constexpr uint32_t ReadmeLen = sizeof(ReadmeText) - 1; // 98 bytes
    uint8_t *readme = root + 32;
    memcpy(readme, "README  TXT", 11);
    readme[11] = 0x20;
    readme[26] = 2;
    readme[27] = 0;
    readme[28] = static_cast<uint8_t>(ReadmeLen & 0xFF);
    readme[29] = static_cast<uint8_t>((ReadmeLen >> 8) & 0xFF);
    readme[30] = static_cast<uint8_t>((ReadmeLen >> 16) & 0xFF);
    readme[31] = static_cast<uint8_t>((ReadmeLen >> 24) & 0xFF);

    memcpy(_disk + (17 * 512), ReadmeText, sizeof(ReadmeText) - 1);
}

bool UsbMscDisk::begin(uint32_t diskSizeBytes, uint32_t sectorSize) {
#if !defined(ARDUINO_USB_MODE)
    Serial.println("Native USB is not supported by this board");
    return false;
#else
    if (diskSizeBytes != 1024 * 1024 || sectorSize != 512) {
        Serial.println("UsbMscDisk requires a 1 MB disk with 512-byte sectors");
        return false;
    }

    _diskSize = diskSizeBytes;
    _sectorSize = sectorSize;

    _partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                          ESP_PARTITION_SUBTYPE_DATA_FAT,
                                          "fatdisk");
    if (_partition == nullptr) {
        Serial.println("FAT disk partition 'fatdisk' was not found");
        return false;
    }
    if (_partition->size < _diskSize) {
        Serial.printf("FAT disk partition is too small: %u bytes\n",
                      (unsigned)_partition->size);
        return false;
    }

    _disk = static_cast<uint8_t *>(heap_caps_malloc(_diskSize,
                                                     MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    if (_disk == nullptr) {
        _disk = static_cast<uint8_t *>(heap_caps_malloc(_diskSize, MALLOC_CAP_8BIT));
    }
    if (_disk == nullptr) {
        Serial.printf("Cannot allocate %u bytes for the FAT disk cache\n",
                      (unsigned)_diskSize);
        return false;
    }

    mscDiskBuffer = _disk;
    fatDiskPartition = _partition;

    esp_err_t readResult = esp_partition_read(_partition, 0, _disk, _diskSize);
    bool needsFormat = readResult != ESP_OK ||
                       _disk[510] != 0x55 || _disk[511] != 0xAA ||
                       memcmp(_disk + 43, "RCM-JIG     ", 11) != 0;
    if (needsFormat) {
        Serial.println("Formatting persistent FAT12 partition");
        formatFat12();

        if (esp_partition_erase_range(_partition, 0, _partition->size) != ESP_OK) {
            Serial.println("Cannot erase FAT disk partition");
            return false;
        }
        if (esp_partition_write(_partition, 0, _disk, _diskSize) != ESP_OK) {
            Serial.println("Cannot write initial FAT disk image");
            return false;
        }
    } else {
        Serial.println("Loaded persistent FAT12 disk from flash");
    }

    MSC.vendorID("RCM-Jig");
    MSC.productID("USB-MSC");
    MSC.productRevision("1.0");
    MSC.onStartStop(mscStartStop);
    MSC.onRead(mscRead);
    MSC.onWrite(mscWrite);
    MSC.mediaPresent(true);
    MSC.isWritable(true);

    MSC.begin(sectorCount(), bytesPerSector());
    USB.begin();

    Serial.printf("USB MSC disk ready: %u bytes, %u sectors\n",
                  (unsigned)_diskSize,
                  (unsigned)sectorCount());
    return true;
#endif
}

bool UsbMscDisk::loadPartition() {
    if (_partition == nullptr) {
        _diskSize = 1024 * 1024;
        _sectorSize = 512;

        _partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                              ESP_PARTITION_SUBTYPE_DATA_FAT,
                                              "fatdisk");
    }

    if (_partition == nullptr || _partition->size < _diskSize) {
        return false;
    }

    if (_disk == nullptr) {
        _disk = static_cast<uint8_t *>(heap_caps_malloc(_diskSize,
                                                         MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        if (_disk == nullptr) {
            _disk = static_cast<uint8_t *>(heap_caps_malloc(_diskSize, MALLOC_CAP_8BIT));
        }
    }

    if (_disk == nullptr) {
        return false;
    }

    mscDiskBuffer = _disk;
    fatDiskPartition = _partition;

    return esp_partition_read(_partition, 0, _disk, _diskSize) == ESP_OK;
}

bool UsbMscDisk::readRootFile(const char *name, std::vector<uint8_t> &data) {
    data.clear();

    if (!loadPartition()) {
        return false;
    }

    const uint8_t bytesPerSectorByte = _disk[11];
    const uint16_t bytesPerSector = bytesPerSectorByte | (uint16_t(_disk[12]) << 8);
    const uint8_t sectorsPerCluster = _disk[13];
    const uint16_t reservedSectors = _disk[14] | (uint16_t(_disk[15]) << 8);
    const uint8_t fats = _disk[16];
    const uint16_t rootEntries = _disk[17] | (uint16_t(_disk[18]) << 8);
    const uint16_t sectorsPerFat = _disk[22] | (uint16_t(_disk[23]) << 8);

    if (bytesPerSector != 512 || reservedSectors != 1 || sectorsPerCluster == 0) {
        return false;
    }

    const uint32_t fatStartSector = reservedSectors;
    const uint32_t rootStartSector = fatStartSector + fats * sectorsPerFat;
    const uint32_t rootDirectorySize = (rootEntries * 32 + bytesPerSector - 1) / bytesPerSector;
    const uint32_t dataStartSector = rootStartSector + rootDirectorySize;

    char dosName[12] = {0};
    const char *dot = strrchr(name, '.');

    size_t stemLength = dot ? static_cast<size_t>(dot - name) : strlen(name);
    if (stemLength > 8) {
        stemLength = 8;
    }

    memset(dosName, ' ', 11);
    for (size_t i = 0; i < stemLength; ++i) {
        dosName[i] = toupper(static_cast<unsigned char>(name[i]));
    }

    size_t extensionLength = dot ? strlen(dot + 1) : 0;
    if (dot && extensionLength > 3) {
        extensionLength = 3;
    }
    for (size_t i = 0; i < extensionLength; ++i) {
        dosName[8 + i] = toupper(static_cast<unsigned char>(dot[1 + i]));
    }

    for (uint32_t entryIndex = 0;
         entryIndex < rootEntries;
         ++entryIndex) {
        uint32_t sectorOffset = entryIndex / (bytesPerSector / 32);
        uint32_t entryOffset = (entryIndex % (bytesPerSector / 32)) * 32;
        const uint8_t *entry = _disk +
                               (rootStartSector + sectorOffset) * bytesPerSector +
                               entryOffset;

        if (entry[0] == 0x00) {
            break;
        }
        if (entry[0] == 0xE5 || entry[11] == 0x0F) {
            continue;
        }

        if (memcmp(entry, dosName, 11) == 0) {
            uint32_t fileSize = entry[28] |
                                (uint32_t(entry[29]) << 8) |
                                (uint32_t(entry[30]) << 16) |
                                (uint32_t(entry[31]) << 24);
            uint16_t cluster = entry[26] | (uint16_t(entry[27]) << 8);

            data.resize(fileSize);
            uint32_t copied = 0;

            while (cluster >= 2 && cluster < 0xFF0 && copied < fileSize) {
                uint32_t clusterBytes = uint32_t(sectorsPerCluster) * bytesPerSector;
                uint32_t sector = dataStartSector +
                                  (cluster - 2) * sectorsPerCluster;
                uint32_t chunk = fileSize - copied;
                if (chunk > clusterBytes) {
                    chunk = clusterBytes;
                }

                memcpy(data.data() + copied,
                       _disk + sector * bytesPerSector,
                       chunk);
                copied += chunk;

                uint32_t fatOffset = cluster + (cluster / 2);
                const uint8_t *fat = _disk + fatStartSector * bytesPerSector;

                if (cluster & 1) {
                    cluster = (uint16_t(fat[fatOffset + 1]) << 4) |
                              ((fat[fatOffset] >> 4) & 0x0F);
                } else {
                    cluster = fat[fatOffset] |
                              ((uint16_t(fat[fatOffset + 1] & 0x0F) << 8));
                }
            }

            data.resize(copied);
            return copied == fileSize;
        }
    }

    return false;
}
