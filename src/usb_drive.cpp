#include "usb_drive.h"
#include "config.h"
#include <string.h>

#if USB_ENABLE

#include <usb/usb_host.h>
#include <usb/usb_types_ch9.h>
#include <usb/usb_types_stack.h>

// =============================================================
// MSC Bulk-Only Transport protocol constants
// =============================================================

#define MSC_CLASS       0x08
#define MSC_SUBCLASS    0x06
#define MSC_PROTOCOL_BOT 0x50

#define SCSI_TEST_UNIT_READY   0x00
#define SCSI_READ_CAPACITY_10  0x25
#define SCSI_READ_10           0x28
#define SCSI_WRITE_10          0x2A
#define SCSI_SYNC_CACHE        0x35

#define CBW_SIGNATURE   0x43425355UL
#define CSW_SIGNATURE   0x53425355UL
#define CBW_SIZE        31
#define CSW_SIZE        13
#define BULK_MPS        64        // Full Speed bulk MPS

#pragma pack(push, 1)
struct CBW {
    uint32_t signature;
    uint32_t tag;
    uint32_t dataLen;
    uint8_t  flags;    // 0x80 = IN
    uint8_t  lun;
    uint8_t  cbLen;
    uint8_t  cmd[16];
};

struct CSW {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t  status;
};
#pragma pack(pop)

// =============================================================
// FAT32 structures (minimal, root-only, SFN 8.3)
// =============================================================

#pragma pack(push, 1)
struct BPB {
    uint8_t  jmpBoot[3];
    char     oemName[8];
    uint16_t bytesPerSec;
    uint8_t  secPerClus;
    uint16_t rsvdSecCnt;
    uint8_t  numFATs;
    uint16_t rootEntCnt;
    uint16_t totSec16;
    uint8_t  media;
    uint16_t fatSz16;
    uint16_t secPerTrk;
    uint16_t numHeads;
    uint32_t hiddSec;
    uint32_t totSec32;
    uint32_t fatSz32;
    uint16_t extFlags;
    uint16_t fsVer;
    uint32_t rootClus;
    uint16_t fsInfo;
    uint16_t bkBootSec;
    uint8_t  reserved[12];
    uint8_t  drvNum;
    uint8_t  reserved1;
    uint8_t  bootSig;
    uint32_t volId;
    char     volLab[11];
    char     fsType[8];
};

struct DirEntry {
    char     name[11];
    uint8_t  attr;
    uint8_t  ntRes;
    uint8_t  crtTimeTenth;
    uint16_t crtTime;
    uint16_t crtDate;
    uint16_t lstAccDate;
    uint16_t clusHi;
    uint16_t wrtTime;
    uint16_t wrtDate;
    uint16_t clusLo;
    uint32_t fileSize;
};
#pragma pack(pop)

// =============================================================
// Driver state
// =============================================================

static struct {
    // USB host library
    bool                        hostInstalled{false};
    usb_host_client_handle_t    clientHdl{nullptr};

    // Connected device
    bool                        devConnected{false};
    usb_device_handle_t         devHdl{nullptr};
    uint8_t                     devAddr{0};

    // MSC interface info
    bool                        mscClaimed{false};
    uint8_t                     bInterfaceNumber{0};
    uint8_t                     epIn{0};
    uint8_t                     epOut{0};

    // Disk info
    bool                        diskReady{false};
    uint32_t                    blockCount{0};
    uint16_t                    blockSize{512};

    // Transfer
    usb_transfer_t*             xfer{nullptr};
    volatile bool               xferDone{false};
    esp_err_t                   xferResult{ESP_OK};
    uint32_t                    xferTag{1};

    // FAT (initialised after disk is ready)
    struct {
        bool    valid{false};
        uint32_t dataStartLba;
        uint32_t clusterSize;
        uint32_t rootCluster;
        uint32_t fatStartLba;
        uint32_t fatSz;
        uint32_t bytesPerSec;
        uint8_t  secPerClus;
    } fat;
} d;

// =============================================================
// USB transfer helpers
// =============================================================

static void xferCb(usb_transfer_t *t) {
    d.xferResult = (t->status == USB_TRANSFER_STATUS_COMPLETED) ? ESP_OK : ESP_FAIL;
    d.xferDone = true;
}

static esp_err_t submitAndWait(usb_transfer_t *t, int timeoutMs) {
    d.xferDone = false;
    esp_err_t err = usb_host_transfer_submit(t);
    if (err != ESP_OK) return err;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
    while (!d.xferDone) {
        if (xTaskGetTickCount() > deadline) return ESP_ERR_TIMEOUT;
        usb_host_client_handle_events(d.clientHdl, 10);
    }
    return d.xferResult;
}

// -------------------------------------------------------------
// Send CBW, transfer data (IN), read CSW — synchronous
// -------------------------------------------------------------
static esp_err_t mscCommand(uint8_t *cmd, uint8_t cmdLen,
                            void *data, uint32_t dataLen,
                            uint8_t direction) // 0x80 = IN, 0x00 = OUT
{
    if (!d.xfer) return ESP_FAIL;

    // --- Phase 1: CBW (OUT) ---
    CBW cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = CBW_SIGNATURE;
    cbw.tag       = d.xferTag++;
    cbw.dataLen   = dataLen;
    cbw.flags     = direction;
    cbw.lun       = 0;
    cbw.cbLen     = cmdLen;
    memcpy(cbw.cmd, cmd, cmdLen);

    usb_transfer_t *t = d.xfer;
    t->device_handle    = d.devHdl;
    t->bEndpointAddress = d.epOut;
    t->callback         = xferCb;
    t->context          = nullptr;
    t->timeout_ms       = 5000;
    t->flags            = 0;  // MSC BOT never sends ZLP
    t->num_bytes        = CBW_SIZE;
    memcpy(t->data_buffer, &cbw, CBW_SIZE);

    esp_err_t err = submitAndWait(t, 5000);
    if (err != ESP_OK) return err;

    // --- Phase 2: Data (direction as specified) ---
    if (dataLen > 0 && data != nullptr) {
        if (direction == 0x80) { // IN
            t->bEndpointAddress = d.epIn;
            t->num_bytes        = dataLen;
            err = submitAndWait(t, 5000);
            if (err != ESP_OK) return err;
            memcpy(data, t->data_buffer, t->actual_num_bytes);
        } else { // OUT
            t->bEndpointAddress = d.epOut;
            t->num_bytes        = dataLen;
            memcpy(t->data_buffer, data, dataLen);
            err = submitAndWait(t, 5000);
            if (err != ESP_OK) return err;
        }
    }

    // --- Phase 3: CSW (IN) ---
    t->bEndpointAddress = d.epIn;
    t->num_bytes        = CSW_SIZE;
    err = submitAndWait(t, 5000);
    if (err != ESP_OK) return err;

    CSW csw;
    memcpy(&csw, t->data_buffer, CSW_SIZE);
    if (csw.signature != CSW_SIGNATURE || csw.status != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

// -------------------------------------------------------------
// SCSI commands
// -------------------------------------------------------------
static esp_err_t mscTestUnitReady() {
    uint8_t cmd[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return mscCommand(cmd, 6, nullptr, 0, 0);
}

static esp_err_t mscReadCapacity() {
    uint8_t cmd[10] = { SCSI_READ_CAPACITY_10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t resp[8];
    esp_err_t err = mscCommand(cmd, 10, resp, 8, 0x80);
    if (err != ESP_OK) return err;
    // Big-endian: first 4 bytes = last LBA, next 4 = block size
    d.blockCount = ((uint32_t)resp[0] << 24) | ((uint32_t)resp[1] << 16)
                 | ((uint32_t)resp[2] << 8)  |  resp[3];
    d.blockSize  = ((uint32_t)resp[4] << 24) | ((uint32_t)resp[5] << 16)
                 | ((uint32_t)resp[6] << 8)  |  resp[7];
    d.blockCount++; // READ CAPACITY returns last LBA, count = last + 1
    return ESP_OK;
}

static esp_err_t mscReadSectors(uint32_t lba, uint32_t count, uint8_t *buf) {
    uint8_t cmd[10] = { SCSI_READ_10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    cmd[2] = (lba >> 24) & 0xFF;
    cmd[3] = (lba >> 16) & 0xFF;
    cmd[4] = (lba >> 8) & 0xFF;
    cmd[5] = lba & 0xFF;
    cmd[7] = (count >> 8) & 0xFF;
    cmd[8] = count & 0xFF;
    return mscCommand(cmd, 10, buf, count * d.blockSize, 0x80);
}

static esp_err_t mscWriteSectors(uint32_t lba, uint32_t count, const uint8_t *buf) {
    uint8_t cmd[10] = { SCSI_WRITE_10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    cmd[2] = (lba >> 24) & 0xFF;
    cmd[3] = (lba >> 16) & 0xFF;
    cmd[4] = (lba >> 8) & 0xFF;
    cmd[5] = lba & 0xFF;
    cmd[7] = (count >> 8) & 0xFF;
    cmd[8] = count & 0xFF;
    return mscCommand(cmd, 10, (void*)buf, count * d.blockSize, 0x00);
}

// =============================================================
// FAT32 sector I/O (uses MSC)
// =============================================================

static bool readSector(uint32_t lba, uint8_t *buf) {
    return mscReadSectors(lba, 1, buf) == ESP_OK;
}

static bool readCluster(uint32_t cluster, uint8_t *buf) {
    uint32_t lba = d.fat.dataStartLba + (cluster - 2) * d.fat.secPerClus;
    for (uint8_t i = 0; i < d.fat.secPerClus; i++) {
        if (!readSector(lba + i, buf + i * d.fat.bytesPerSec)) return false;
    }
    return true;
}

static uint32_t nextCluster(uint32_t cluster) {
    uint32_t fatOff  = cluster * 4; // FAT32: 4 bytes per entry
    uint32_t fatSec  = fatOff / d.fat.bytesPerSec;
    uint32_t fatOffs = fatOff % d.fat.bytesPerSec;
    uint8_t sector[512];
    if (!readSector(d.fat.fatStartLba + fatSec, sector)) return 0x0FFFFFFF;
    uint32_t val;
    memcpy(&val, sector + fatOffs, 4);
    return val & 0x0FFFFFFF;
}

static bool isEoc(uint32_t clus) { return clus >= 0x0FFFFFF8; }
#define CLUS_END 0x0FFFFFFF

// ─── Sector write wrapper ────────────────────────────────────
static bool writeSector(uint32_t lba, const uint8_t *data) {
    return mscWriteSectors(lba, 1, data) == ESP_OK;
}

// ─── Read/write a FAT32 entry (4 bytes per cluster) ──────────
static bool readFATEntry(uint32_t cluster, uint32_t &val) {
    uint32_t off = cluster * 4;
    uint32_t sec = d.fat.fatStartLba + off / d.fat.bytesPerSec;
    uint32_t pos = off % d.fat.bytesPerSec;
    if (pos + 4 > d.fat.bytesPerSec) return false;
    uint8_t sector[512];
    if (!readSector(sec, sector)) return false;
    memcpy(&val, sector + pos, 4);
    val &= 0x0FFFFFFF;
    return true;
}

static bool writeFATEntry(uint32_t cluster, uint32_t val) {
    uint32_t off = cluster * 4;
    uint32_t sec = d.fat.fatStartLba + off / d.fat.bytesPerSec;
    uint32_t pos = off % d.fat.bytesPerSec;
    if (pos + 4 > d.fat.bytesPerSec) return false;
    uint8_t sector[512];
    if (!readSector(sec, sector)) return false;
    val &= 0x0FFFFFFF;
    memcpy(sector + pos, &val, 4);
    if (!writeSector(sec, sector)) return false;
    // Update FAT² mirror if present
    if (d.fat.fatSz > 0) {
        uint32_t sec2 = (d.fat.fatStartLba + d.fat.fatSz) + off / d.fat.bytesPerSec;
        if (sec2 != sec) {
            uint8_t sector2[512];
            if (readSector(sec2, sector2)) {
                memcpy(sector2 + pos, &val, 4);
                writeSector(sec2, sector2);
            }
        }
    }
    return true;
}

// ─── Find and allocate a free cluster ────────────────────────
// Returns 0 if none free.
static uint32_t findFreeCluster() {
    uint32_t totalClus = (d.fat.fatSz * d.fat.bytesPerSec) / 4;
    for (uint32_t c = 2; c < totalClus; c++) {
        uint32_t v;
        if (!readFATEntry(c, v)) return 0;
        if (v == 0) return c;  // free
    }
    return 0;
}

// ─── Write data to contiguous clusters ───────────────────────
// Allocates clusters, writes data, sets up FAT chain.
// Returns first cluster number, or 0 on failure.
static uint32_t writeClusters(const uint8_t *data, size_t len) {
    uint32_t secPerClus = d.fat.secPerClus;
    uint32_t bytesPerClus = secPerClus * d.fat.bytesPerSec;
    uint32_t needed = (len + bytesPerClus - 1) / bytesPerClus;
    if (needed == 0) return 0;

    // Allocate cluster numbers
    uint32_t *clusters = (uint32_t*)malloc(needed * sizeof(uint32_t));
    if (!clusters) return 0;
    for (uint32_t i = 0; i < needed; i++) {
        clusters[i] = findFreeCluster();
        if (clusters[i] == 0) {
            // Free already-allocated clusters
            for (uint32_t j = 0; j < i; j++)
                writeFATEntry(clusters[j], 0);
            free(clusters);
            return 0;
        }
    }

    // Mark all as end-of-chain initially
    for (uint32_t i = 0; i < needed; i++)
        writeFATEntry(clusters[i], CLUS_END);

    // Chain them
    for (uint32_t i = 0; i + 1 < needed; i++)
        writeFATEntry(clusters[i], clusters[i + 1]);

    // Write data
    size_t remaining = len;
    for (uint32_t i = 0; i < needed && remaining > 0; i++) {
        uint32_t lba = d.fat.dataStartLba + (clusters[i] - 2) * secPerClus;
        for (uint8_t s = 0; s < secPerClus && remaining > 0; s++) {
            size_t chunk = (remaining > d.fat.bytesPerSec) ? d.fat.bytesPerSec : remaining;
            uint8_t sector[512];
            memset(sector, 0, sizeof(sector));
            memcpy(sector, data + i * bytesPerClus + s * d.fat.bytesPerSec, chunk);
            if (!writeSector(lba + s, sector)) {
                free(clusters);
                return 0;
            }
            remaining -= chunk;
        }
    }

    uint32_t first = clusters[0];
    free(clusters);
    return first;
}

// ─── Find a directory entry by SFN in a specific directory ───
// Returns the cluster containing the entry, and offset within it.
static bool findDirEntryBySFN(uint32_t dirCluster, const char sfn[11],
                              uint32_t &outSec, uint32_t &outOff)
{
    uint32_t clus = dirCluster;
    while (!isEoc(clus)) {
        uint32_t lbaBase = d.fat.dataStartLba + (clus - 2) * d.fat.secPerClus;
        for (uint8_t s = 0; s < d.fat.secPerClus; s++) {
            uint8_t sector[512];
            if (!readSector(lbaBase + s, sector)) return false;
            for (int i = 0; i < (int)(d.fat.bytesPerSec / 32); i++) {
                DirEntry *de = (DirEntry *)&sector[i * 32];
                if (de->name[0] == 0x00) return false;
                if (de->name[0] == 0xE5) continue;
                if (de->attr & 0x0F) continue; // skip LFN
                if (memcmp(de->name, sfn, 11) == 0) {
                    outSec = lbaBase + s;
                    outOff = i * 32;
                    return true;
                }
            }
        }
        clus = nextCluster(clus);
        if (clus == 0) return false;
    }
    return false;
}

// ─── Find a free 32-byte slot in a directory cluster chain ───
// Returns true and sets outSec/outOff if found.
static bool findFreeDirSlot(uint32_t dirCluster, uint32_t &outSec, uint32_t &outOff) {
    uint32_t clus = dirCluster;
    while (!isEoc(clus)) {
        uint32_t lbaBase = d.fat.dataStartLba + (clus - 2) * d.fat.secPerClus;
        for (uint8_t s = 0; s < d.fat.secPerClus; s++) {
            uint8_t sector[512];
            if (!readSector(lbaBase + s, sector)) return false;
            for (int i = 0; i < (int)(d.fat.bytesPerSec / 32); i++) {
                DirEntry *de = (DirEntry *)&sector[i * 32];
                if (de->name[0] == 0x00 || de->name[0] == 0xE5) {
                    outSec = lbaBase + s;
                    outOff = i * 32;
                    return true;
                }
            }
        }
        clus = nextCluster(clus);
        if (clus == 0) return false;
    }
    // Need to allocate a new cluster for the directory
    uint32_t newClus = findFreeCluster();
    if (newClus == 0) return false;
    writeFATEntry(newClus, CLUS_END);
    // Link to end of chain
    clus = dirCluster;
    while (!isEoc(clus)) {
        uint32_t next = nextCluster(clus);
        if (isEoc(next)) {
            writeFATEntry(clus, newClus);
            break;
        }
        clus = next;
    }
    // Zero new cluster
    uint32_t lbaBase = d.fat.dataStartLba + (newClus - 2) * d.fat.secPerClus;
    uint8_t zero[512];
    memset(zero, 0, sizeof(zero));
    for (uint8_t s = 0; s < d.fat.secPerClus; s++)
        writeSector(lbaBase + s, zero);
    outSec = lbaBase;
    outOff = 0;
    return true;
}

// ─── Write a directory entry ─────────────────────────────────
static bool writeDirEntryAt(uint32_t sec, uint32_t off, const DirEntry &entry) {
    uint8_t sector[512];
    if (!readSector(sec, sector)) return false;
    memcpy(sector + off, &entry, sizeof(DirEntry));
    return writeSector(sec, sector);
}

// ─── Build SFN 8.3 from a filename string (no path) ─────────
// Fills sfn[11] with space-padded 8.3.
static void pathToSFN(const char *name, char sfn[11]) {
    memset(sfn, ' ', 11);
    int idx = 0;
    while (*name && *name != '.' && idx < 8)
        sfn[idx++] = toupper((unsigned char)*name++);
    if (*name == '.') {
        name++;
        idx = 8;
        while (*name && idx < 11)
            sfn[idx++] = toupper((unsigned char)*name++);
    }
}

// ─── Navigate path components to find parent dir cluster ─────
// Given "/GCODE/FILE.GCO", sets parentCluster to GCODE's cluster,
// sets outFilename to "FILE.GCO" within path. Returns false if
// any directory component doesn't exist.
static bool resolvePath(const char *path, uint32_t &parentCluster, const char *&outName) {
    if (!path || *path != '/') return false;
    path++; // skip leading /
    parentCluster = d.fat.rootCluster;

    // Find last component
    const char *lastSlash = strrchr(path, '/');
    if (lastSlash == nullptr) {
        // Just a root file like "FILE.GCO"
        outName = path;
        return true;
    }

    // Navigate through directory components
    char dirName[64];
    const char *p = path;
    while (p < lastSlash) {
        const char *slash = strchr(p, '/');
        if (slash == nullptr || slash > lastSlash) slash = lastSlash;
        size_t dlen = slash - p;
        if (dlen == 0 || dlen >= sizeof(dirName)) return false;
        memcpy(dirName, p, dlen);
        dirName[dlen] = '\0';
        p = slash + 1;

        // Look for this directory in parentCluster
        char sfn[11];
        pathToSFN(dirName, sfn);
        uint32_t sec, off;
        if (!findDirEntryBySFN(parentCluster, sfn, sec, off))
            return false;
        uint8_t sector[512];
        if (!readSector(sec, sector)) return false;
        DirEntry *de = (DirEntry *)&sector[off];
        if (!(de->attr & 0x10)) return false; // not a directory
        parentCluster = ((uint32_t)de->clusHi << 16) | de->clusLo;
    }
    outName = lastSlash + 1;
    return true;
}

static bool loadFileByCluster(uint32_t startCluster, PSRAMBuffer &buf) {
    uint32_t clus = startCluster;
    while (!isEoc(clus)) {
        uint32_t lba = d.fat.dataStartLba + (clus - 2) * d.fat.secPerClus;
        for (uint8_t s = 0; s < d.fat.secPerClus; s++) {
            uint8_t sector[512];
            if (!readSector(lba + s, sector)) return false;
            if (!buf.write(sector, d.fat.bytesPerSec)) return false;
        }
        clus = nextCluster(clus);
        if (clus == 0 || clus >= 0x0FFFFFF8) break;
    }
    return true;
}

// =============================================================
// FAT32 initialisation
// =============================================================

static void initFAT() {
    uint8_t sector[512];
    if (!readSector(0, sector)) return;

    uint32_t partLba = 0;
    if (sector[510] == 0x55 && sector[511] == 0xAA) {
        uint8_t type = sector[446 + 4];
        if (type == 0x0B || type == 0x0C) {
            memcpy(&partLba, sector + 446 + 8, 4);
        } else {
            for (int i = 1; i < 4; i++) {
                uint8_t t = sector[446 + i * 16 + 4];
                if (t == 0x0B || t == 0x0C) {
                    memcpy(&partLba, sector + 446 + i * 16 + 8, 4);
                    break;
                }
            }
        }
    }

    if (!readSector(partLba, sector)) return;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return;

    BPB *bpb = (BPB *)sector;
    d.fat.bytesPerSec  = bpb->bytesPerSec;
    d.fat.secPerClus   = bpb->secPerClus;
    d.fat.fatSz        = bpb->fatSz32 ? bpb->fatSz32 : bpb->fatSz16;
    d.fat.fatStartLba  = partLba + bpb->rsvdSecCnt;
    d.fat.rootCluster  = bpb->rootClus;
    d.fat.dataStartLba = partLba + bpb->rsvdSecCnt + bpb->numFATs * d.fat.fatSz;
    d.fat.clusterSize  = d.fat.secPerClus * d.fat.bytesPerSec;
    d.fat.valid        = true;
}

// =============================================================
// Client event callback
// =============================================================

static void clientEventCb(const usb_host_client_event_msg_t *msg, void *arg) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        d.devAddr      = msg->new_dev.address;
        d.devConnected = true;
    }
    // DEV_GONE is detected on transfer failure
}

// =============================================================
// Enumerate and claim MSC interface
// =============================================================

static bool claimMSC() {
    // Open device
    if (usb_host_device_open(d.clientHdl, d.devAddr, &d.devHdl) != ESP_OK)
        return false;
    // Get config descriptor to find MSC interface
    const usb_config_desc_t *cfgDesc = nullptr;
    if (usb_host_get_active_config_descriptor(d.devHdl, &cfgDesc) != ESP_OK)
        return false;

    int offset = 0;
    const usb_standard_desc_t *desc = usb_parse_next_descriptor(
        (const usb_standard_desc_t *)cfgDesc, cfgDesc->wTotalLength, &offset);

    while (desc) {
        if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)desc;
            if (intf->bInterfaceClass    == MSC_CLASS &&
                intf->bInterfaceSubClass == MSC_SUBCLASS &&
                intf->bInterfaceProtocol == MSC_PROTOCOL_BOT) {

                d.bInterfaceNumber = intf->bInterfaceNumber;

                // Claim the interface
                if (usb_host_interface_claim(d.clientHdl, d.devHdl,
                        d.bInterfaceNumber, 0) != ESP_OK)
                    return false;
                d.mscClaimed = true;

                // Walk endpoints
                int epOff = offset;
                const usb_standard_desc_t *epDesc = usb_parse_next_descriptor(
                    desc, cfgDesc->wTotalLength, &epOff);
                while (epDesc && epDesc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                    const usb_ep_desc_t *ep = (const usb_ep_desc_t *)epDesc;
                    if (ep->bmAttributes == USB_TRANSFER_TYPE_BULK) {
                        if (ep->bEndpointAddress & 0x80) {
                            d.epIn = ep->bEndpointAddress;
                        } else {
                            d.epOut = ep->bEndpointAddress;
                        }
                    }
                    epOff++;
                    epDesc = usb_parse_next_descriptor(
                        (const usb_standard_desc_t *)epDesc, cfgDesc->wTotalLength, &epOff);
                }
                return d.epIn && d.epOut;
            }
        }
        offset++;
        desc = usb_parse_next_descriptor(desc, cfgDesc->wTotalLength, &offset);
    }
    return false;
}

// =============================================================
// Public API
// =============================================================

bool USBDrive::begin() {
    // Install USB host library
    usb_host_config_t hostCfg = {
        .skip_phy_setup = false,
        .intr_flags     = 0,
    };
    if (usb_host_install(&hostCfg) != ESP_OK) {
        _ready = false;
        return false;
    }
    d.hostInstalled = true;

    // Register client
    usb_host_client_config_t clientCfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = clientEventCb,
            .callback_arg = nullptr,
        },
    };
    if (usb_host_client_register(&clientCfg, &d.clientHdl) != ESP_OK) {
        _ready = false;
        return false;
    }

    // Allocate transfer (max sector size is sufficient)
    if (usb_host_transfer_alloc(512, 0, &d.xfer) != ESP_OK) {
        _ready = false;
        return false;
    }

    _ready = true;
    return true;
}

bool USBDrive::enumerate(USBEnumCallback cb, void *userData) {
    if (!d.diskReady || !d.fat.valid) return false;

    uint32_t clus = d.fat.rootCluster;
    while (!isEoc(clus)) {
        uint32_t lba = d.fat.dataStartLba + (clus - 2) * d.fat.secPerClus;
        for (uint8_t s = 0; s < d.fat.secPerClus; s++) {
            uint8_t sector[512];
            if (!readSector(lba + s, sector)) return false;
            for (int i = 0; i < (int)(d.fat.bytesPerSec / 32); i++) {
                DirEntry *de = (DirEntry *)&sector[i * 32];
                if (de->name[0] == 0x00) return true;
                if (de->name[0] == 0xE5) continue;
                if (de->attr & 0x08) continue;
                if (de->attr & 0x0F) continue;

                USBFileEntry entry;
                memset(&entry, 0, sizeof(entry));
                int pos = 0;
                for (int j = 0; j < 8 && de->name[j] != ' '; j++)
                    if (de->name[j] >= 0x20 && de->name[j] <= 0x7E)
                        entry.name[pos++] = de->name[j];
                if (de->name[8] != ' ') {
                    entry.name[pos++] = '.';
                    for (int j = 8; j < 11 && de->name[j] != ' '; j++)
                        if (de->name[j] >= 0x20 && de->name[j] <= 0x7E)
                            entry.name[pos++] = de->name[j];
                }
                entry.name[pos] = '\0';
                entry.isDir = (de->attr & 0x10);
                entry.size  = de->fileSize;
                if (!cb(entry, userData)) return true;
            }
        }
        clus = nextCluster(clus);
        if (clus == 0) return false;
    }
    return true;
}

bool USBDrive::loadFile(const char *path, PSRAMBuffer &buf) {
    if (!d.diskReady || !d.fat.valid) return false;
    buf.clear();

    uint32_t parentCluster;
    const char *fname;
    if (!resolvePath(path, parentCluster, fname)) return false;

    char sfn[11];
    pathToSFN(fname, sfn);

    uint32_t sec, off;
    if (!findDirEntryBySFN(parentCluster, sfn, sec, off)) return false;

    uint8_t sector[512];
    if (!readSector(sec, sector)) return false;
    DirEntry *de = (DirEntry *)&sector[off];
    if (de->attr & 0x10) return false; // is a directory

    uint32_t fc = ((uint32_t)de->clusHi << 16) | de->clusLo;
    return loadFileByCluster(fc, buf);
}

bool USBDrive::exists(const char *path) {
    if (!d.diskReady || !d.fat.valid) return false;
    uint32_t parentCluster;
    const char *fname;
    if (!resolvePath(path, parentCluster, fname)) return false;
    char sfn[11];
    pathToSFN(fname, sfn);
    uint32_t sec, off;
    return findDirEntryBySFN(parentCluster, sfn, sec, off);
}

struct ListCtx {
    Print *out;
    int count;
};

static bool listCB(const USBFileEntry &entry, void *userData) {
    auto *ctx = (ListCtx *)userData;
    ctx->out->print(entry.isDir ? "[DIR] " : "      ");
    ctx->out->println(entry.name);
    ctx->count++;
    return true;
}

bool USBDrive::listFiles(Print &out, const char *) {
    if (!d.diskReady || !d.fat.valid) return false;
    ListCtx ctx = {&out, 0};
    uint32_t clus = d.fat.rootCluster;
    while (!isEoc(clus)) {
        uint32_t lba = d.fat.dataStartLba + (clus - 2) * d.fat.secPerClus;
        for (uint8_t s = 0; s < d.fat.secPerClus; s++) {
            uint8_t sector[512];
            if (!readSector(lba + s, sector)) return false;
            for (int i = 0; i < (int)(d.fat.bytesPerSec / 32); i++) {
                DirEntry *de = (DirEntry *)&sector[i * 32];
                if (de->name[0] == 0x00) return ctx.count > 0;
                if (de->name[0] == 0xE5) continue;
                if (de->attr & 0x08) continue;
                if (de->attr & 0x0F) continue;
                USBFileEntry entry;
                memset(&entry, 0, sizeof(entry));
                int pos = 0;
                for (int j = 0; j < 8 && de->name[j] != ' '; j++)
                    if (de->name[j] >= 0x20 && de->name[j] <= 0x7E)
                        entry.name[pos++] = de->name[j];
                if (de->name[8] != ' ') {
                    entry.name[pos++] = '.';
                    for (int j = 8; j < 11 && de->name[j] != ' '; j++)
                        if (de->name[j] >= 0x20 && de->name[j] <= 0x7E)
                            entry.name[pos++] = de->name[j];
                }
                entry.name[pos] = '\0';
                entry.isDir = (de->attr & 0x10);
                entry.size  = de->fileSize;
                if (!listCB(entry, &ctx)) return true;
            }
        }
        clus = nextCluster(clus);
        if (clus == 0) return false;
    }
    return ctx.count > 0;
}

// =============================================================
// Directory & file write
// =============================================================

bool USBDrive::makeDir(const char *path) {
    if (!d.diskReady || !d.fat.valid) return false;

    // Only root-level directories for now
    if (!path || *path != '/') return false;
    const char *name = path + 1;
    if (*name == '\0') return true; // root already exists
    if (strchr(name, '/')) return false; // no nested dirs

    // Check if already exists
    char sfn[11];
    pathToSFN(name, sfn);
    {
        uint32_t sec, off;
        if (findDirEntryBySFN(d.fat.rootCluster, sfn, sec, off))
            return true; // already exists
    }

    // Allocate a cluster for the directory
    uint32_t clus = findFreeCluster();
    if (clus == 0) return false;
    writeFATEntry(clus, CLUS_END);

    // Initialise directory cluster: . and ..
    uint8_t sector[512];
    memset(sector, 0, sizeof(sector));

    DirEntry dot;
    memset(&dot, 0, sizeof(dot));
    dot.name[0] = '.';
    memset(dot.name + 1, ' ', 10);
    dot.attr = 0x10;
    dot.clusHi = (clus >> 16) & 0xFFFF;
    dot.clusLo = clus & 0xFFFF;
    memcpy(sector, &dot, sizeof(dot));

    DirEntry dotdot;
    memset(&dotdot, 0, sizeof(dotdot));
    dotdot.name[0] = '.';
    dotdot.name[1] = '.';
    memset(dotdot.name + 2, ' ', 9);
    dotdot.attr = 0x10;
    dotdot.clusHi = (d.fat.rootCluster >> 16) & 0xFFFF;
    dotdot.clusLo = d.fat.rootCluster & 0xFFFF;
    memcpy(sector + 32, &dotdot, sizeof(dotdot));

    uint32_t lbaBase = d.fat.dataStartLba + (clus - 2) * d.fat.secPerClus;
    for (uint8_t s = 0; s < d.fat.secPerClus; s++) {
        if (s == 0) {
            if (!writeSector(lbaBase, sector)) return false;
        } else {
            uint8_t zero[512];
            memset(zero, 0, sizeof(zero));
            if (!writeSector(lbaBase + s, zero)) return false;
        }
    }

    // Create directory entry in root
    DirEntry de;
    memset(&de, 0, sizeof(de));
    memcpy(de.name, sfn, 11);
    de.attr = 0x10; // directory
    de.clusHi = (clus >> 16) & 0xFFFF;
    de.clusLo = clus & 0xFFFF;

    uint32_t sec, off;
    if (!findFreeDirSlot(d.fat.rootCluster, sec, off)) return false;
    return writeDirEntryAt(sec, off, de);
}

bool USBDrive::writeFile(const char *path, const uint8_t *data, size_t len) {
    if (!d.diskReady || !d.fat.valid) return false;
    if (!data || len == 0) return false;

    uint32_t parentCluster;
    const char *fname;
    if (!resolvePath(path, parentCluster, fname)) return false;

    char sfn[11];
    pathToSFN(fname, sfn);

    // Delete existing entry so we can overwrite
    {
        uint32_t sec, off;
        if (findDirEntryBySFN(parentCluster, sfn, sec, off)) {
            uint8_t sector[512];
            if (readSector(sec, sector)) {
                sector[off] = 0xE5; // mark as deleted
                writeSector(sec, sector);
            }
        }
    }

    // Write data to clusters
    uint32_t firstClus = writeClusters(data, len);
    if (firstClus == 0) return false;

    // Create directory entry
    DirEntry de;
    memset(&de, 0, sizeof(de));
    memcpy(de.name, sfn, 11);
    de.attr = 0x20; // archive
    de.fileSize = len;
    de.clusHi = (firstClus >> 16) & 0xFFFF;
    de.clusLo = firstClus & 0xFFFF;

    uint32_t sec, off;
    if (!findFreeDirSlot(parentCluster, sec, off)) return false;
    if (!writeDirEntryAt(sec, off, de)) return false;

    return true;
}

// =============================================================
// Polling — must be called from loop()
// =============================================================

void pollUSB() {
    if (!d.hostInstalled) return;

    // Process USB events
    usb_host_client_handle_events(d.clientHdl, 0);

    // New device seen?
    if (d.devConnected && d.devHdl == nullptr) {
        if (claimMSC()) {
            // Wait for device to be ready
            for (int retry = 0; retry < 50; retry++) {
                if (mscTestUnitReady() == ESP_OK) break;
                delay(100);
                usb_host_client_handle_events(d.clientHdl, 0);
            }
            if (mscReadCapacity() == ESP_OK) {
                d.diskReady = true;
                initFAT();
            }
        }
        d.devConnected = false; // prevent re-try until re-plug
    }

    // If device gone, reset state
    // (detected when transfers fail — handled in mscCommand)
}

// =============================================================
// Streaming file read (for firmware update)
// =============================================================

bool USBDrive::openFile(const char *path) {
    if (!d.diskReady || !d.fat.valid) return false;

    uint32_t parentCluster;
    const char *fname;
    if (!resolvePath(path, parentCluster, fname)) return false;

    char sfn[11];
    pathToSFN(fname, sfn);

    uint32_t sec, off;
    if (!findDirEntryBySFN(parentCluster, sfn, sec, off)) return false;

    uint8_t sector[512];
    if (!readSector(sec, sector)) return false;
    DirEntry *de = (DirEntry *)&sector[off];
    if (de->attr & 0x10) return false;

    _streamCluster     = ((uint32_t)de->clusHi << 16) | de->clusLo;
    _streamSector      = 0;
    _streamOffset      = 0;
    _streamRemaining   = de->fileSize;
    _streamFileSize    = de->fileSize;
    _streamOpen        = true;
    return true;
}

size_t USBDrive::readFile(uint8_t *buf, size_t len) {
    if (!_streamOpen || !d.diskReady || !d.fat.valid) return 0;
    if (_streamRemaining == 0) return 0;

    size_t toRead = (len < _streamRemaining) ? len : _streamRemaining;
    size_t totalRead = 0;

    while (totalRead < toRead) {
        // Calculate current sector LBA
        uint32_t lba = d.fat.dataStartLba
                     + (_streamCluster - 2) * d.fat.secPerClus
                     + _streamSector;

        uint8_t sector[512];
        if (!readSector(lba, sector)) {
            _streamOpen = false;
            return totalRead;
        }

        size_t fromThisSector = d.fat.bytesPerSec - _streamOffset;
        if (fromThisSector > toRead - totalRead)
            fromThisSector = toRead - totalRead;

        memcpy(buf + totalRead, sector + _streamOffset, fromThisSector);
        totalRead += fromThisSector;
        _streamRemaining -= fromThisSector;
        _streamOffset += fromThisSector;

        // Advance to next sector or cluster
        if (_streamOffset >= d.fat.bytesPerSec) {
            _streamOffset = 0;
            _streamSector++;
            if (_streamSector >= d.fat.secPerClus) {
                _streamSector = 0;
                uint32_t next = nextCluster(_streamCluster);
                if (isEoc(next) || next == 0) {
                    _streamOpen = false;
                    break;
                }
                _streamCluster = next;
            }
        }
    }

    return totalRead;
}

void USBDrive::closeFile() {
    _streamOpen = false;
    _streamCluster = 0;
    _streamSector = 0;
    _streamOffset = 0;
    _streamRemaining = 0;
    _streamFileSize = 0;
}

#else // USB_ENABLE == 0

bool USBDrive::begin() { return false; }
bool USBDrive::enumerate(USBEnumCallback, void*) { return false; }
bool USBDrive::loadFile(const char*, PSRAMBuffer&) { return false; }
bool USBDrive::exists(const char*) { return false; }
bool USBDrive::listFiles(Print&, const char*) { return false; }
bool USBDrive::makeDir(const char*) { return false; }
bool USBDrive::writeFile(const char*, const uint8_t*, size_t) { return false; }
bool USBDrive::openFile(const char*) { return false; }
size_t USBDrive::readFile(uint8_t*, size_t) { return 0; }
void USBDrive::closeFile() {}
void pollUSB() {}

#endif // USB_ENABLE
