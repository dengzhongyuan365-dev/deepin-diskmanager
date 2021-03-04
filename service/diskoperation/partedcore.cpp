/**
 * @copyright 2020-2020 Uniontech Technology Co., Ltd.
 *
 * @file partedcore.cpp
 *
 * @brief 磁盘操作类
 *
 * @date 2020-09-03 17:49
 *
 * Author: liweigang  <liweigang@uniontech.com>
 *
 * Maintainer: liweigang  <liweigang@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "partedcore.h"
#include "fsinfo.h"
#include "mountinfo.h"
#include "partition.h"
#include "procpartitionsinfo.h"
#include "filesystems/filesystem.h"

#include <sys/mount.h>
#include <unistd.h>
#include <fcntl.h>

#include <QDebug>

namespace DiskManager {
//hdparm可检测，显示与设定IDE或SCSI硬盘的参数。
//udevadm可检测设备热插拔
//static bool udevadm_found = false;
static bool hdparm_found = false;
const std::time_t SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS = 1;
//const std::time_t SETTLE_DEVICE_APPLY_MAX_WAIT_SECONDS = 10;
SupportedFileSystems *PartedCore::m_supportedFileSystems = nullptr;

PartedCore::PartedCore(QObject *parent)
    : QObject(parent)
{
    connect(this, &PartedCore::refreshDeviceInfo, this, &PartedCore::onRefreshDeviceInfo);

    qDebug() << __FUNCTION__ << "^^1";

    for (PedPartitionFlag flag = ped_partition_flag_next(static_cast<PedPartitionFlag>(NULL));
            flag; flag = ped_partition_flag_next(flag))
        m_flags.push_back(flag);

    qDebug() << __FUNCTION__ << "^^2";

    findSupportedCore();

    qDebug() << __FUNCTION__ << "^^3";

    m_supportedFileSystems = new SupportedFileSystems();
    //Determine file system support capabilities for the first time
    m_supportedFileSystems->findSupportedFilesystems();

    qDebug() << __FUNCTION__ << "^^4";

    probeDeviceInfo();

    qDebug() << __FUNCTION__ << "^^5";
}

PartedCore::~PartedCore()
{
    delete m_supportedFileSystems;
    m_supportedFileSystems = nullptr;
}

void PartedCore::findSupportedCore()
{
    //udevadm_found = !Utils::findProgramInPath("udevadm").isEmpty();
    hdparm_found = !Utils::findProgramInPath("hdparm").isEmpty();
}

bool PartedCore::supportedFileSystem(FSType fstype)
{
    return m_supportedFileSystems->getFsObject(fstype) != nullptr;
}

const FS &PartedCore::getFileSystem(FSType fstype) const
{
    return m_supportedFileSystems->getFsSupport(fstype);
}

FileSystem *PartedCore::getFileSystemObject(FSType fstype)
{
    return m_supportedFileSystems->getFsObject(fstype);
}

//bool PartedCore::filesystem_resize_disallowed(const Partition &partition)
//{
//    if (partition.fstype == FS_LVM2_PV) {
//        //        //The LVM2 PV can't be resized when it's a member of an export VG
//        //        QString vgname = LVM2_PV_Info::get_vg_name(partition.getPath());
//        //        if (vgname .isEmpty())
//        //            return false ;
//        //        return LVM2_PV_Info::is_vg_exported(vgname);
//    }
//    return false;
//}

void PartedCore::insertUnallocated(const QString &devicePath, QVector<Partition *> &partitions, Sector start, Sector end, Byte_Value sectorSize, bool insideExtended)
{
    //if there are no partitions at all..
    if (partitions.empty()) {
        Partition *partitionTemp = new Partition();
        partitionTemp->setUnallocated(devicePath, start, end, sectorSize, insideExtended);
        partitions.push_back(partitionTemp);
        return;
    }

    //start <---> first partition start
    if ((partitions.front()->m_sectorStart - start) > (MEBIBYTE / sectorSize)) {
        Sector tempEnd = partitions.front()->m_sectorStart - 1;
        Partition *partitionTemp = new Partition();
        partitionTemp->setUnallocated(devicePath, start, tempEnd, sectorSize, insideExtended);
        partitions.insert(partitions.begin(), partitionTemp);
    }

    //look for gaps in between
    for (int t = 0; t < partitions.size() - 1; t++) {
        if (((partitions.at(t + 1)->m_sectorStart - partitions.at(t)->m_sectorEnd - 1) > (MEBIBYTE / sectorSize))
                || ((partitions.at(t + 1)->m_type != TYPE_LOGICAL) // Only show exactly 1 MiB if following partition is not logical.
                    && ((partitions.at(t + 1)->m_sectorStart - partitions.at(t)->m_sectorEnd - 1) == (MEBIBYTE / sectorSize)))) {
            Sector tempStart = partitions.at(t)->m_sectorEnd + 1;
            Sector tempEnd = partitions.at(t + 1)->m_sectorStart - 1;
            Partition *partitionTemp = new Partition();
            partitionTemp->setUnallocated(devicePath, tempStart, tempEnd,
                                            sectorSize, insideExtended);
            partitions.insert(partitions.begin() + (++t), partitionTemp);
            // partitions.insert_adopt(partitions.begin() + ++t, partition_temp);
        }
    }
    partitions.back();
    //last partition end <---> end
    if ((end - partitions.back()->m_sectorEnd) >= (MEBIBYTE / sectorSize)) {
        Sector tempStart = partitions.back()->m_sectorEnd + 1;
        Partition *partitionTemp = new Partition();
        partitionTemp->setUnallocated(devicePath, tempStart, end, sectorSize, insideExtended);
        partitions.push_back(partitionTemp);
    }
}

void PartedCore::setFlags(Partition &partition, PedPartition *lpPartition)
{
    for (int t = 0; t < m_flags.size(); t++) {
        if (ped_partition_is_flag_available(lpPartition, m_flags[t]) && ped_partition_get_flag(lpPartition, m_flags[t]))
            partition.m_flags.push_back(ped_partition_flag_get_name(m_flags[t]));
    }
}

FS_Limits PartedCore::getFileSystemLimits(FSType fstype, const Partition &partition)
{
    FileSystem *pFileSystem = m_supportedFileSystems->getFsObject(fstype);
    FS_Limits fsLimits;
    if (pFileSystem != nullptr)
        fsLimits = pFileSystem->getFilesystemLimits(partition);
    return fsLimits;
}

void PartedCore::probeDeviceInfo(const QString &)
{
    m_inforesult.clear();
    m_devicemap.clear();
    QVector<QString> devicepaths;
    qDebug() << __FUNCTION__ << "**1";
    devicepaths.clear();
    BlockSpecial::clearCache();
    qDebug() << __FUNCTION__ << "**2";
    ProcPartitionsInfo::loadCache();
    qDebug() << __FUNCTION__ << "**3";
    FsInfo::loadCache();
    qDebug() << __FUNCTION__ << "**4";
    qDebug() << __FUNCTION__ << "**5";
    MountInfo::loadCache();
    qDebug() << __FUNCTION__ << "**6";
    ped_device_probe_all();
    qDebug() << __FUNCTION__ << "**7";
    PedDevice *lpDevice = ped_device_get_next(nullptr);
    while (lpDevice) {
        /* TO TRANSLATORS: looks like   Confirming /dev/sda */
        qDebug() << QString("Confirming %1").arg(lpDevice->path);

        //only add this device if we can read the first sector (which means it's a real device)
        if (useableDevice(lpDevice))
            devicepaths.push_back(lpDevice->path);
        qDebug() << lpDevice->path;
        lpDevice = ped_device_get_next(lpDevice);
    }
    qDebug() << __FUNCTION__ << "devicepaths size=" << devicepaths.size();
    std::sort(devicepaths.begin(), devicepaths.end());
    qDebug() << __FUNCTION__ << "**8";
    for (int t = 0; t < devicepaths.size(); t++) {
        /*TO TRANSLATORS: looks like Searching /dev/sda partitions */
        Device tempDevice;
        setDeviceFromDisk(tempDevice, devicepaths[t]);
        m_devicemap.insert(devicepaths.at(t), tempDevice);
    }
    qDebug() << __FUNCTION__ << "**9";
    for (auto it = m_devicemap.begin(); it != m_devicemap.end(); it++) {
        DeviceInfo devinfo = it.value().getDeviceInfo();
        for (int i = 0; i < it.value().m_partitions.size(); i++) {
            Partition pat = *(it.value().m_partitions.at(i));
            PartitionInfo partinfo = pat.getPartitionInfo();
            for (auto i = pat.m_flags.begin();i != pat.m_flags.end();i++) {
                if(*i == "hidden") {
                    partinfo.m_flag = 1;
                }
                else {
                    partinfo.m_flag = 0;
                }
            }
            if (pat.m_type == TYPE_EXTENDED) {
                devinfo.partition.push_back(partinfo);
                for (int k = 0; k < pat.m_logicals.size(); k++) {
                    Partition plogic = *(pat.m_logicals.at(k));
                    partinfo = plogic.getPartitionInfo();
                    devinfo.partition.push_back(partinfo);
                }
            } else {
                devinfo.partition.push_back(partinfo);
            }
        }
        m_inforesult.insert(devinfo.m_path, devinfo);
    }
    qDebug() << __FUNCTION__ << m_inforesult.count();
    qDebug() << __FUNCTION__ << "**10";
}

bool PartedCore::useableDevice(const PedDevice *lpDevice)
{
    Q_ASSERT(nullptr != lpDevice);
    char *buf = static_cast<char *>(malloc(lpDevice->sector_size));
    if (!buf)
        return false;

    // Must be able to read from the first sector before the disk device is considered
    // useable in GParted.
    bool success = false;
    int fd = open(lpDevice->path, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        ssize_t bytesRead = read(fd, buf, lpDevice->sector_size);
        success = (bytesRead == lpDevice->sector_size);
        close(fd);
    }
    free(buf);
    return success;
}

void PartedCore::setDeviceFromDisk(Device &device, const QString &devicePath)
{
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;
    if (getDevice(devicePath, lpDevice, true)) {
        device.m_heads = lpDevice->bios_geom.heads;
        device.m_length = lpDevice->length;
        device.m_path = devicePath;
        device.m_model = lpDevice->model;
        device.m_sectorSize = lpDevice->sector_size;
        device.m_sectors = lpDevice->bios_geom.sectors;
        device.m_cylinders = lpDevice->bios_geom.cylinders;
        device.m_cylsize = device.m_heads * device.m_sectors;
        setDeviceSerialNumber(device);
        if (device.m_cylsize < (MEBIBYTE / device.m_sectorSize))
            device.m_cylsize = MEBIBYTE / device.m_sectorSize;

        FSType fstype = detectFilesystem(lpDevice, nullptr);
        if (fstype != FS_UNKNOWN) {
            device.m_diskType = "none";
            device.m_maxPrims = 1;
            setDeviceOnePartition(device, lpDevice, fstype);
        } else if (getDisk(lpDevice, lpDisk, false)) {
            // Partitioned drive (excluding "loop"), as recognised by libparted
            if (lpDisk && lpDisk->type && lpDisk->type->name && strcmp(lpDisk->type->name, "loop") != 0) {
                device.m_diskType = lpDisk->type->name;
                device.m_maxPrims = ped_disk_get_max_primary_partition_count(lpDisk);

                // Determine if partition naming is supported.
                if (ped_disk_type_check_feature(lpDisk->type, PED_DISK_TYPE_PARTITION_NAME)) {
                    device.enablePartitionNaming(
                        Utils::getMaxPartitionNameLength(device.m_diskType));
                }

                setDevicePartitions(device, lpDevice, lpDisk);

                if (device.m_highestBusy) {
                    device.m_readonly = !commitToOs(lpDisk);
                }
            }
            // Drive just containing libparted "loop" signature and nothing
            // else.  (Actually any drive reported by libparted as "loop" but
            // not recognised by blkid on the whole disk device).
            else if (lpDisk && lpDisk->type && lpDisk->type->name && strcmp(lpDisk->type->name, "loop") == 0) {
                device.m_diskType = lpDisk->type->name;
                device.m_maxPrims = 1;

                // Create virtual partition covering the whole disk device
                // with unknown contents.
                Partition *partition_temp = new Partition();
                partition_temp->setUnpartitioned(device.m_path,
                                                  lpDevice->path,
                                                  FS_UNKNOWN,
                                                  device.m_length,
                                                  device.m_sectorSize,
                                                  false);
                // Place unknown file system message in this partition.
                device.m_partitions.push_back(partition_temp);
            }
            // Unrecognised, unpartitioned drive.
            else {
                device.m_diskType = "unrecognized";
                device.m_maxPrims = 1;

                Partition *partition_temp = new Partition();
                partition_temp->setUnpartitioned(device.m_path,
                                                  "", // Overridden with "unallocated"
                                                  FS_UNALLOCATED,
                                                  device.m_length,
                                                  device.m_sectorSize,
                                                  false);
                device.m_partitions.push_back(partition_temp);
            }
        }
        destroyDeviceAndDisk(lpDevice, lpDisk);
    }
}

bool PartedCore::getDevice(const QString &devicePath, PedDevice *&lpDevice, bool flush)
{
    lpDevice = ped_device_get(devicePath.toStdString().c_str());
    if (lpDevice) {
        if (flush)
            // Force cache coherency before going on to read the partition
            // table so that libparted reading the whole disk device and the
            // file system tools reading the partition devices read the same
            // data.
            flushDevice(lpDevice);
        return true;
    }
    return false;
}

bool PartedCore::getDisk(PedDevice *&lpDevice, PedDisk *&lpDisk, bool strict)
{
    if (lpDevice) {
        lpDisk = ped_disk_new(lpDevice);

        // (#762941)(!46) After ped_disk_new() wait for triggered udev rules to
        // to complete which remove and re-add all the partition specific /dev
        // entries to avoid FS specific commands failing because they happen to
        // be running when the needed /dev/PTN entries don't exist.
        //settleDevice(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS);
        QString out, err;
        Utils::executCmd(QString("udevadm settle --timeout=%1").arg(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS), out, err);
        // if ! disk and writable it's probably a HD without disklabel.
        // We return true here and deal with them in
        // GParted_Core::setDeviceFromDisk().
        if (lpDisk || (!strict && !lpDevice->read_only))
            return true;

        destroyDeviceAndDisk(lpDevice, lpDisk);
    }

    return false;
}

void PartedCore::destroyDeviceAndDisk(PedDevice *&lpDevice, PedDisk *&lpDisk)
{
    if (lpDisk)
        ped_disk_destroy(lpDisk);
    lpDisk = nullptr;

    if (lpDevice)
        ped_device_destroy(lpDevice);
    lpDevice = nullptr;
}

bool PartedCore::infoBelongToPartition(const Partition &partition, const PartitionInfo info)
{
    bool belong = false;
    if (info.m_sectorEnd == partition.m_sectorEnd && info.m_sectorStart == partition.m_sectorStart)
        belong = true;
    return belong;
}

bool PartedCore::getDeviceAndDisk(const QString &devicePath, PedDevice *&lpDevice, PedDisk *&lpDisk, bool strict, bool flush)
{
    if (getDevice(devicePath, lpDevice, flush)) {
        return getDisk(lpDevice, lpDisk, strict);
    }

    return false;
}

bool PartedCore::commit(PedDisk *lpDisk)
{
    bool opened = ped_device_open(lpDisk->dev);

    bool succes = ped_disk_commit_to_dev(lpDisk);

    succes = commitToOs(lpDisk) && succes;

    if (opened) {
        ped_device_close(lpDisk->dev);
        // Wait for udev rules to complete and partition device nodes to settle
        // from this ped_device_close().
        //settleDevice(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS);
        QString out, err;
        Utils::executCmd(QString("udevadm settle --timeout=%1").arg(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS), out, err);
    }

    return succes;
}

PedPartition *PartedCore::getLpPartition(const PedDisk *lpDisk, const Partition &partition)
{
    if (partition.m_type == TYPE_EXTENDED)
        return ped_disk_extended_partition(lpDisk);
    return ped_disk_get_partition_by_sector(lpDisk, partition.getSector());
}

void PartedCore::setDeviceSerialNumber(Device &device)
{
    if (!hdparm_found)
        // Serial number left blank when the hdparm command is not installed.
        return;

    QString output, error;
    Utils::executCmd(QString("hdparm -I %1").arg(device.m_path), output, error);
    if (error.isEmpty()) {
        // hdparm reported an error message to stderr.  Assume it's a device
        // without a hard drive serial number.
        //
        // Using hdparm -I to query Linux software RAID arrays and BIOS fake RAID
        // arrays, both devices without their own hard drive serial numbers,
        // produce this error:
        //     HDIO_DRIVE_CMD(identify) failed: Inappropriate ioctl for device
        //
        // And querying USB flash drives, also a device type without their own
        // hard drive serial numbers, generates this error:
        //     SG_IO: bad/missing sense data, sb[]:  70 00 05 00 00 00 00 0a ...
        device.m_serialNumber = "none";
    } else {
        QString serialNumber = Utils::regexpLabel(output, "(?<=Serial Number:).*(?=\n)").trimmed();
        if (!serialNumber.isEmpty())
            device.m_serialNumber = serialNumber;
    }
    // Otherwise serial number left blank when not found in the hdparm output.
}

void PartedCore::setDeviceOnePartition(Device &device, PedDevice *lpDevice, FSType fstype)
{
    device.m_partitions.clear();
    QString path(lpDevice->path);
    bool partitionIsBusy = isBusy(fstype, path);

    Partition *partitionTemp = nullptr;
    if (fstype == FS_LUKS) {
        partitionTemp = nullptr; //= new PartitionLUKS();
    } else
        partitionTemp = new Partition();
    if (nullptr == partitionTemp)
        return;
    partitionTemp->setUnpartitioned(device.m_path,
                                      path,
                                      fstype,
                                      device.m_length,
                                      device.m_sectorSize,
                                      partitionIsBusy);

    //        if ( fstype == FS_LUKS )
    //            set_luks_partition( *dynamic_cast<PartitionLUKS *>( partition_temp ) );

    if (partitionTemp->m_busy)
        device.m_highestBusy = 1;

    setPartitionLabelAndUuid(*partitionTemp);
    setMountPoints(*partitionTemp);
    setUsedSectors(*partitionTemp, nullptr);
    device.m_partitions.push_back(partitionTemp);
}

void PartedCore::setPartitionLabelAndUuid(Partition &partition)
{
    QString partitionPath = partition.getPath();
    readLabel(partition);
    if (!partition.filesystemLabelKnown()) {
        bool labelFound = false;
        QString label = FsInfo::getLabel(partitionPath, labelFound);
        if (labelFound)
            partition.setFilesystemLabel(label);
    }

    // Retrieve file system UUID.  Use cached method first in an effort to speed up
    // device scanning.
    partition.m_uuid = FsInfo::getUuid(partitionPath);
    if (partition.m_uuid.isEmpty()) {
        readUuid(partition);
    }
}

bool PartedCore::isBusy(FSType fstype, const QString &path, const PedPartition *lpPartition)
{
    FileSystem *pFilesystem = nullptr;
    bool busy = false;
    if (nullptr != lpPartition) {
        busy = ped_partition_is_busy(lpPartition);
    }
    if (!busy && supportedFileSystem(fstype)) {
        switch (getFileSystem(fstype).busy) {
        case FS::GPARTED:
            //Search GParted internal mounted partitions map
            busy = MountInfo::isDevMounted(path);
            break;
        case FS::EXTERNAL:
            //Call file system specific method
            pFilesystem = getFileSystemObject(fstype);
            if (pFilesystem)
                busy = pFilesystem->isBusy(path);
            break;

        default:
            break;
        }
    }

    return busy;
}

void PartedCore::readLabel(Partition &partition)
{
    FileSystem *pFilesystem = nullptr;
    switch (getFileSystem(partition.m_fstype).read_label) {
    case FS::EXTERNAL:
        pFilesystem = getFileSystemObject(partition.m_fstype);
        if (pFilesystem)
            pFilesystem->readLabel(partition);
        break;

    default:
        break;
    }
}

void PartedCore::readUuid(Partition &partition)
{
    FileSystem *pFilesystem = nullptr;
    switch (getFileSystem(partition.m_fstype).read_uuid) {
    case FS::EXTERNAL:
        pFilesystem = getFileSystemObject(partition.m_fstype);
        if (pFilesystem)
            pFilesystem->readUuid(partition);
        break;

    default:
        break;
    }
}

void PartedCore::setMountPoints(Partition &partition)
{
    //DMRaid dmraid ; //Use cache of dmraid device information
    if (partition.m_fstype == FS_LVM2_PV) {
        //        QString vgname = LVM2_PV_Info::get_vg_name(partition.getPath());
        //        if (! vgname.isEmpty())
        //            partition.addMountPoint(vgname);
    } else if (partition.m_fstype == FS_LINUX_SWRAID) {
        //        QString array_path = SWRaid_Info::get_array(partition.getPath());
        //        if (! array_path.isEmpty())
        //            partition.addMountPoint(array_path);
    } else if (partition.m_fstype == FS_ATARAID) {
        //        QString array_path = SWRaid_Info::get_array(partition.getPath());
        //        if (! array_path.isEmpty()) {
        //            partition.addMountPoint(array_path);
        //        } else {
        //            array_path = dmraid.get_array(partition.getPath());
        //            if (! array_path.isEmpty())
        //                partition.addMountPoint(array_path);
        //        }
    } else if (partition.m_fstype == FS_LUKS) {
        //        LUKS_Mapping mapping = LUKS_Info::get_cache_entry(partition.getPath());
        //        if (! mapping.name.isEmpty())
        //            partition.addMountPoint(DEV_MAPPER_PATH + mapping.name);
    }
    // Swap spaces don't have mount points so don't bother trying to add them.
    else if (partition.m_fstype != FS_LINUX_SWAP) {
        if (partition.m_busy) {
            // Normal device, not DMRaid device
            if (setMountPointsHelper(partition, partition.getPath()))
                return;

            qDebug() << __FUNCTION__ << "xxxUnable to find mount point";
        } else { // Not busy file system
            partition.addMountPoints(MountInfo::getFileSystemTableMountpoints(partition.getPath()));
        }
    }
}

bool PartedCore::setMountPointsHelper(Partition &partition, const QString &path)
{
    QString searchPath;
    if (partition.m_fstype == FS_BTRFS)
        searchPath = path; //btrfs::get_mount_device( path ) ;
    else
        searchPath = path;

    const QVector<QString> &mountpoints = MountInfo::getMountedMountpoints(searchPath);
    if (mountpoints.size()) {
        partition.addMountPoints(mountpoints);
        partition.m_fsReadonly = MountInfo::isDevMountedReadonly(searchPath);
        return true;
    }

    return false;
}

void PartedCore::setUsedSectors(Partition &partition, PedDisk *lpDisk)
{
    Q_UNUSED(lpDisk)
    if (supportedFileSystem(partition.m_fstype)) {
        FileSystem *pFilesystem = nullptr;
        if (partition.m_busy) {
            switch (getFileSystem(partition.m_fstype).online_read) {
            case FS::EXTERNAL:
                pFilesystem = getFileSystemObject(partition.m_fstype);
                if (pFilesystem)
                    pFilesystem->setUsedSectors(partition);
                break;
            case FS::GPARTED:
                mountedFileSystemSetUsedSectors(partition);
                break;
            default:
                break;
            }
        } else { // Not busy file system
            switch (getFileSystem(partition.m_fstype).read) {
            case FS::EXTERNAL:
                pFilesystem = getFileSystemObject(partition.m_fstype);
                if (pFilesystem)
                    pFilesystem->setUsedSectors(partition);
                break;
#ifdef HAVE_LIBPARTED_FS_RESIZE
            case FS::LIBPARTED:
                if (lp_disk)
                    LpSetUsedSectors(partition, lp_disk);
                break;
#endif
            default:
                break;
            }
        }

        Sector unallocated;
        // Only confirm that the above code succeeded in setting the sector usage
        // values for this base Partition object, hence the explicit call to the
        // base Partition class sectorUsageKnown() method.  For LUKS this avoids
        // calling derived PartitionLUKS class sectorUsageKnown() which also
        // checks for known sector usage in the encrypted file system.  But that
        // wasn't set by the above code so in the case of luks/unknown would
        // produce a false positive.
        if (!partition.sectorUsageKnown()) {
            if (!Utils::getFileSystemSoftWare(partition.m_fstype).isEmpty()) {
                QString msg("The following list of software packages is required for %1 file system support:  %2.");
                msg = msg.arg(Utils::fileSystemTypeToString(partition.m_fstype)).arg(Utils::getFileSystemSoftWare(partition.m_fstype));
                qDebug() << __FUNCTION__ << msg;
            }

        } else if ((unallocated = partition.getSectorsUnallocated()) > 0) {
            /* TO TRANSLATORS: looks like   1.28GiB of unallocated space within the partition. */
            QString temp("%1 of unallocated space within the partition.");
            temp = temp.arg(Utils::formatSize(unallocated, partition.m_sectorSize));
            FS fs = getFileSystem(partition.m_fstype);
            if (fs.check != FS::NONE && fs.grow != FS::NONE) {
                temp.append("To grow the file system to fill the partition, select the partition and choose the menu item:\n");
                temp.append("Partition --> Check.");
            }
        }

//        if (filesystem_resize_disallowed(partition)) {
//            //            QString temp = getFileSystemObject(partition.fstype)
//            //                                 ->get_custom_text(CTEXT_RESIZE_DISALLOWED_WARNING);
//        }
    } else {
        // Set usage of mounted but unsupported file systems.
        if (partition.m_busy)
            mountedFileSystemSetUsedSectors(partition);
    }
}

void PartedCore::mountedFileSystemSetUsedSectors(Partition &partition)
{
    if (partition.getMountPoints().size() > 0 && MountInfo::isDevMounted(partition.getPath())) {
        Byte_Value fs_size;
        Byte_Value fs_free;
        if (Utils::getMountedFileSystemUsage(partition.getMountPoint(), fs_size, fs_free) == 0)
            partition.setSectorUsage(fs_size / partition.m_sectorSize,
                                       fs_free / partition.m_sectorSize);
    }
}

void PartedCore::setDevicePartitions(Device &device, PedDevice *lpDevice, PedDisk *lpDisk)
{
    int EXTINDEX = -1;
    device.m_partitions.clear();

    PedPartition *lpPartition = ped_disk_next_partition(lpDisk, nullptr);
    while (lpPartition) {
        Partition *partitionTemp = nullptr;
        bool partitionIsBusy = false;
        FSType fstype = FS_UNKNOWN;
        QString partitionPath;
        switch (lpPartition->type) {
        case PED_PARTITION_NORMAL:
        case PED_PARTITION_LOGICAL:
            fstype = detectFilesystem(lpDevice, lpPartition);
            partitionPath = getPartitionPath(lpPartition);
            partitionIsBusy = isBusy(fstype, partitionPath, lpPartition);
            qDebug() << partitionIsBusy << lpPartition->num << lpPartition->disk->dev->path;
            //            if (fstype == FS_LUKS)
            //                partition_temp = new PartitionLUKS();
            //            else
            partitionTemp = new Partition();
            partitionTemp->set(device.m_path,
                                partitionPath,
                                lpPartition->num,
                                (lpPartition->type == PED_PARTITION_NORMAL) ? TYPE_PRIMARY
                                : TYPE_LOGICAL,
                                fstype,
                                lpPartition->geom.start,
                                lpPartition->geom.end,
                                device.m_sectorSize,
                                (lpPartition->type == PED_PARTITION_LOGICAL),
                                partitionIsBusy);

            setFlags(*partitionTemp, lpPartition);

            //if (fstype == FS_LUKS)
            // set_luks_partition(*dynamic_cast<PartitionLUKS *>(partition_temp));

            if (partitionTemp->m_busy && partitionTemp->m_partitionNumber > device.m_highestBusy)
                device.m_highestBusy = partitionTemp->m_partitionNumber;
            break;

        case PED_PARTITION_EXTENDED:
            partitionPath = getPartitionPath(lpPartition);

            partitionTemp = new Partition();
            partitionTemp->set(device.m_path,
                                partitionPath,
                                lpPartition->num,
                                TYPE_EXTENDED,
                                FS_EXTENDED,
                                lpPartition->geom.start,
                                lpPartition->geom.end,
                                device.m_sectorSize,
                                false,
                                false);

            setFlags(*partitionTemp, lpPartition);

            EXTINDEX = device.m_partitions.size();
            break;

        default:
            qDebug() << ped_partition_is_busy(lpPartition) << lpPartition->num << lpPartition->disk->dev->path;
            // Ignore libparted reported partitions with other type
            // bits set.
            break;
        }

        // Only for libparted reported partition types that we care about: NORMAL,
        // LOGICAL, EXTENDED
        if (partitionTemp != nullptr) {
            setPartitionLabelAndUuid(*partitionTemp);
            setMountPoints(*partitionTemp);
            setUsedSectors(*partitionTemp, lpDisk);

            // Retrieve partition name
            if (device.partitionNamingSupported())
                partitionTemp->m_name = ped_partition_get_name(lpPartition);

            if (!partitionTemp->m_insideExtended)
                device.m_partitions.push_back(partitionTemp);
            else
                device.m_partitions[EXTINDEX]->m_logicals.push_back(partitionTemp);
        }

        //next partition (if any)
        lpPartition = ped_disk_next_partition(lpDisk, lpPartition);
    }

    if (EXTINDEX > -1) {
        insertUnallocated(device.m_path,
                           device.m_partitions.at(EXTINDEX)->m_logicals,
                           device.m_partitions.at(EXTINDEX)->m_sectorStart,
                           device.m_partitions.at(EXTINDEX)->m_sectorEnd,
                           device.m_sectorSize,
                           true);

        //Set busy status of extended partition if and only if
        //  there is at least one busy logical partition.
        for (int t = 0; t < device.m_partitions.at(EXTINDEX)->m_logicals.size(); t++) {
            if (device.m_partitions.at(EXTINDEX)->m_logicals.at(t)->m_busy) {
                device.m_partitions.at(EXTINDEX)->m_busy = true;
                break;
            }
        }
    }

    insertUnallocated(device.m_path, device.m_partitions, 0, device.m_length - 1, device.m_sectorSize, false);
}

bool PartedCore::flushDevice(PedDevice *lpDevice)
{
    bool success = false;
    if (ped_device_open(lpDevice)) {
        success = ped_device_sync(lpDevice);
        ped_device_close(lpDevice);
        // (!46) Wait for udev rules to complete after this ped_device_open() and
        // ped_device_close() pair to avoid busy /dev/DISK entry when running file
        // system specific querying commands on the whole disk device in the call
        // sequence after getDevice() in setDeviceFromDisk().
        //settleDevice(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS);
        QString out, err;
        Utils::executCmd(QString("udevadm settle --timeout=%1").arg(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS), out, err);
    }
    return success;
}

//void PartedCore::settleDevice(std::time_t timeout)
//{
//    //如果支持udevadm
//    //udevadm settle [options]　　查看udev事件队列，如果所有事件全部处理完就退出。timeout超时时间
//    if (udevadm_found) {
//        QString out, err;
//        Utils::executCmd(QString("udevadm settle --timeout=%1").arg(timeout), out, err);
//    } else
//        sleep(timeout);
//}

bool PartedCore::commitToOs(PedDisk *lpDisk)
{
    bool succes;
    succes = ped_disk_commit_to_os(lpDisk);
    // Wait for udev rules to complete and partition device nodes to settle from above
    // ped_disk_commit_to_os() initiated kernel update of the partitions.
    //settleDevice(timeout);
    return succes;
}

FSType PartedCore::detectFilesystem(PedDevice *lpDevice, PedPartition *lpPartition)
{
    QString fsname = "";
    QString path;
    // Will query whole disk device using methods: (Q1) RAID, (Q2) blkid,
    // (Q4) internal
    if (lpPartition)
        path = getPartitionPath(lpPartition);
    else
        path = lpDevice->path;

    fsname = FsInfo::getFileSystemType(path);
    FSType fstype = FS_UNKNOWN;
    if (fsname.isEmpty() && lpPartition && lpPartition->fs_type)
        fsname = lpPartition->fs_type->name;
    if (!fsname.isEmpty()) {
        fstype = Utils::stringToFileSystemType(fsname);
        qDebug() << fstype;
        if (fstype != FS_UNKNOWN)
            return fstype;
    }

    fstype = detectFilesystemInternal(path, lpDevice->sector_size);
    if (fstype != FS_UNKNOWN)
        return fstype;

    //no file system found....
    QString temp("Unable to detect file system! Possible reasons are:\n- ");
    temp.append("The file system is damaged \n- ");
    temp.append("The file system is unknown to GParted \n-");
    temp.append("There is no file system available (unformatted) \n- ");
    temp.append(QString("The device entry %1 is missing").arg(path));
    qDebug() << __FUNCTION__ << temp;
    return FS_UNKNOWN;
}

FSType PartedCore::detectFilesystemInternal(const QString &path, Byte_Value sectorSize)
{
    char magic1[16]; // Big enough for largest signatures[].sig1 or sig2
    char magic2[16];
    FSType fstype = FS_UNKNOWN;

    char *buf = static_cast<char *>(malloc(sectorSize));
    if (!buf)
        return FS_UNKNOWN;

    int fd = open(path.toStdString().c_str(), O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        free(buf);
        return FS_UNKNOWN;
    }

    struct {
        Byte_Value offset1;
        const char *sig1;
        Byte_Value offset2;
        const char *sig2;
        FSType fstype;
    } signatures[] = {
        //offset1, sig1              , offset2, sig2  , fstype
        {0LL, "LUKS\xBA\xBE", 0LL, nullptr, FS_LUKS},
        {3LL, "-FVE-FS-", 0LL, nullptr, FS_BITLOCKER},
        {0LL, "\x52\x56\xBE\x1B", 0LL, nullptr, FS_GRUB2_CORE_IMG},
        {0LL, "\x52\x56\xBE\x6F", 0LL, nullptr, FS_GRUB2_CORE_IMG},
        {0LL, "\x52\xE8\x28\x01", 0LL, nullptr, FS_GRUB2_CORE_IMG},
        {0LL, "\x52\xBF\xF4\x81", 0LL, nullptr, FS_GRUB2_CORE_IMG},
        {0LL, "\x52\x56\xBE\x63", 0LL, nullptr, FS_GRUB2_CORE_IMG},
        {0LL, "\x52\x56\xBE\x56", 0LL, nullptr, FS_GRUB2_CORE_IMG},
        {24LL, "\x01\x00", 32LL, "NXSB", FS_APFS},
        {512LL, "LABELONE", 536LL, "LVM2", FS_LVM2_PV},
        {1030LL, "\x34\x34", 0LL, nullptr, FS_NILFS2},
        {65536LL, "ReIsEr4", 0LL, nullptr, FS_REISER4},
        {65600LL, "_BHRfS_M", 0LL, nullptr, FS_BTRFS}
    };
    // For simple BitLocker recognition consider validation of BIOS Parameter block
    // fields unnecessary.
    // *   Detecting BitLocker
    //     http://blogs.msdn.com/b/si_team/archive/2006/10/26/detecting-bitlocker.aspx
    //
    // Recognise GRUB2 core.img just by any of the possible first 4 bytes of x86 CPU
    // instructions it starts with.
    // *   bootinfoscript v0.77 line 1990  [GRUB2 core.img possible staring 4 bytes]
    //     https://github.com/arvidjaar/bootinfoscript/blob/009f509d59e2f0d39b8d44692e2a81720f5af7b6/bootinfoscript#L1990
    //
    // Simple APFS recognition based on matching the following fields in the
    // superblock:
    // 1)  Object type is OBJECT_TYPE_NX_SUPERBLOCK, lower 16-bits of the object type
    //     field is 0x0001 stored as little endian bytes 0x01, 0x00.
    //     WARNING: The magic signatures are defined as NUL terminated strings so the
    //     below code only does a 1-byte match for 0x01, rather than a 2-byte match
    //     for 0x01, 0x00.
    // 2)  4 byte magic "NXSB".
    // *   Apple File System Reference
    //     https://developer.apple.com/support/apple-file-system/Apple-File-System-Reference.pdf

    Byte_Value prevReadOffset = -1;
    memset(buf, 0, sectorSize);

    for (unsigned int i = 0; i < sizeof(signatures) / sizeof(signatures[0]); i++) {
        const size_t len1 = std::min((signatures[i].sig1 == nullptr) ? 0U : strlen(signatures[i].sig1),
                                     sizeof(magic1));
        const size_t len2 = std::min((signatures[i].sig2 == nullptr) ? 0U : strlen(signatures[i].sig2),
                                     sizeof(magic2));
        // NOTE: From this point onwards signatures[].sig1 and .sig2 are treated
        // as character buffers of known lengths len1 and len2, not NUL terminated
        // strings.
        if (len1 == 0UL || (signatures[i].sig2 != nullptr && len2 == 0UL))
            continue; // Don't allow 0 length signatures to match

        Byte_Value readOffset = signatures[i].offset1 / sectorSize * sectorSize;

        // Optimisation: only read new sector when it is different to the
        // previously read sector.
        if (readOffset != prevReadOffset) {
            if (lseek(fd, readOffset, SEEK_SET) == readOffset && read(fd, buf, sectorSize) == sectorSize) {
                prevReadOffset = readOffset;
            } else {
                // Outside block device boundaries or other error.
                continue;
            }
        }

        memcpy(magic1, buf + signatures[i].offset1 % sectorSize, len1);

        // WARNING: This assumes offset2 is in the same sector as offset1
        if (signatures[i].sig2 != nullptr)
            memcpy(magic2, buf + signatures[i].offset2 % sectorSize, len2);

        if (memcmp(magic1, signatures[i].sig1, len1) == 0 && (signatures[i].sig2 == nullptr || memcmp(magic2, signatures[i].sig2, len2) == 0)) {
            fstype = signatures[i].fstype;
            break;
        }
    }

    close(fd);
    free(buf);

    return fstype;
}

QString PartedCore::getPartitionPath(PedPartition *lpPartition)
{
    char *lpPath; //we have to free the result of ped_partition_get_path()
    QString partitionPath("Partition path not found");

    lpPath = ped_partition_get_path(lpPartition);
    if (lpPath != nullptr) {
        partitionPath = lpPath;
        free(lpPath);
    }
    return partitionPath;
}

void PartedCore::LpSetUsedSectors(Partition &partition, PedDisk *lpDisk)
{
    PedFileSystem *fs = nullptr;
    PedConstraint *constraint = nullptr;

    if (lpDisk) {
        PedPartition *lpPartition = ped_disk_get_partition_by_sector(lpDisk, partition.getSector());

        if (lpPartition) {
            fs = ped_file_system_open(&lpPartition->geom);

            if (fs) {
                constraint = ped_file_system_get_resize_constraint(fs);
                if (constraint) {
                    partition.setSectorUsage(fs->geom->length,
                                               fs->geom->length - constraint->min_size);

                    ped_constraint_destroy(constraint);
                }
                ped_file_system_close(fs);
            }
        }
    }
}

bool PartedCore::namePartition(const Partition &partition)
{
    QString msg;
    if (partition.m_name.isEmpty())
        msg = QString("Clear partition name on %1").arg(partition.getPath());
    else
        msg = QString("Set partition name to \"%1\" on %2").arg(partition.m_name).arg(partition.getPath());
    qDebug() << __FUNCTION__ << msg;

    bool success = false;
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;
    if (getDeviceAndDisk(partition.m_devicePath, lpDevice, lpDisk)) {
        PedPartition *lpPartition = ped_disk_get_partition_by_sector(lpDisk, partition.getSector());
        if (lpPartition) {
            success = ped_partition_set_name(lpPartition, partition.m_name.toLatin1())
                      && commit(lpDisk);
        }
    }
    return success;
}

bool PartedCore::eraseFilesystemSignatures(const Partition &partition)
{
    if (partition.m_fstype == FS_LUKS && partition.m_busy) {
        qDebug() << __FUNCTION__ << "partition contains open LUKS encryption for an erase file system signatures only step";
        return false;
    }

    bool overallSuccess = false;
    qDebug() << __FUNCTION__ << QString("clear old file system signatures in %1").arg(partition.getPath());

    //Get device, disk & partition and open the device.  Allocate buffer and fill with
    //  zeros.  Buffer size is the greater of 4 KiB and the sector size.
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;
    PedPartition *lpPartition = nullptr;
    bool deviceIsOpen = false;
    Byte_Value bufsize = 4LL * KIBIBYTE;
    char *buf = nullptr;
    if (getDevice(partition.m_devicePath, lpDevice)) {
        if (partition.m_type == TYPE_UNPARTITIONED) {
            // Virtual partition spanning whole disk device
            overallSuccess = true;
        } else if (getDisk(lpDevice, lpDisk)) {
            // Partitioned device
            lpPartition = ped_disk_get_partition_by_sector(lpDisk, partition.getSector());
            overallSuccess = (lpPartition != nullptr);
        }

        if (overallSuccess && ped_device_open(lpDevice)) {
            deviceIsOpen = true;

            bufsize = std::max(bufsize, lpDevice->sector_size);
            buf = static_cast<char *>(malloc(bufsize));
            if (buf)
                memset(buf, 0, bufsize);
        }
        overallSuccess &= deviceIsOpen;
    }
    struct {
        Byte_Value offset; //Negative offsets work backwards from the end of the partition
        Byte_Value rounding; //Minimum desired rounding for offset
        Byte_Value length;
    } ranges[] = {
        //offset           , rounding       , length
        {0LL, 1LL, 512LL * KIBIBYTE}, // All primary super blocks
        {64LL * MEBIBYTE, 1LL, 4LL * KIBIBYTE}, // Btrfs super block mirror copy
        {256LL * GIBIBYTE, 1LL, 4LL * KIBIBYTE}, // Btrfs super block mirror copy
        {1LL * PEBIBYTE, 1LL, 4LL * KIBIBYTE}, // Btrfs super block mirror copy
        {-512LL * KIBIBYTE, 256LL * KIBIBYTE, 512LL * KIBIBYTE}, // ZFS labels L2 and L3
        {-64LL * KIBIBYTE, 64LL * KIBIBYTE, 4LL * KIBIBYTE}, // SWRaid metadata 0.90 super block
        {-8LL * KIBIBYTE, 4LL * KIBIBYTE, 8LL * KIBIBYTE} // @-8K SWRaid metadata 1.0 super block
        // and @-4K Nilfs2 secondary super block
    };
    for (unsigned int i = 0; overallSuccess && i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        //Rounding is performed in multiples of the sector size because writes are in whole sectors.

        Byte_Value roundingSize = Utils::ceilSize(ranges[i].rounding, lpDevice->sector_size);
        Byte_Value byteOffset;
        Byte_Value byteLen;
        if (ranges[i].offset >= 0LL) {
            byteOffset = Utils::floorSize(ranges[i].offset, roundingSize);
            byteLen = Utils::ceilSize(ranges[i].offset + ranges[i].length, lpDevice->sector_size) - byteOffset;
        } else { //Negative offsets
            Byte_Value notionalOffset = Utils::floorSize(partition.getByteLength() + ranges[i].offset, ranges[i].rounding);
            byteOffset = Utils::floorSize(notionalOffset, roundingSize);
            byteLen = Utils::ceilSize(notionalOffset + ranges[i].length, lpDevice->sector_size) - byteOffset;
        }
        //Limit range to partition size.
        if (byteOffset + byteLen <= 0LL) {
            //Byte range entirely before partition start.  Programmer error!
            continue;
        } else if (byteOffset < 0) {
            //Byte range spans partition start.  Trim to fit.
            byteLen += byteOffset;
            byteOffset = 0LL;
        }
        if (byteOffset >= partition.getByteLength()) {
            //Byte range entirely after partition end.  Ignore.
            continue;
        } else if (byteOffset + byteLen > partition.getByteLength()) {
            //Byte range spans partition end.  Trim to fit.
            byteLen = partition.getByteLength() - byteOffset;
        }

        Byte_Value written = 0LL;
        bool zeroSuccess = false;
        if (deviceIsOpen && buf) {
            // Start sector of the whole disk device or the partition
            Sector ptnStart = 0LL;
            if (lpPartition)
                ptnStart = lpPartition->geom.start;

            while (written < byteLen) {
                //Write in bufsize amounts.  Last write may be smaller but
                //  will still be a whole number of sectors.
                Byte_Value amount = std::min(bufsize, byteLen - written);
                zeroSuccess = ped_device_write(lpDevice, buf,
                                                ptnStart + (byteOffset + written) / lpDevice->sector_size,
                                                amount / lpDevice->sector_size);
                if (!zeroSuccess)
                    break;
                written += amount;
            }
        }
        overallSuccess &= zeroSuccess;
    }
    if (buf)
        free(buf);

    if (overallSuccess) {
        bool flushSuccess = false;
        if (deviceIsOpen) {
            flushSuccess = ped_device_sync(lpDevice);
            ped_device_close(lpDevice);
            //settleDevice(SETTLE_DEVICE_PROBE_MAX_WAIT_SECONDS);
        }
        overallSuccess &= flushSuccess;
    }
    destroyDeviceAndDisk(lpDevice, lpDisk);
    return overallSuccess;
}

bool PartedCore::setPartitionType(const Partition &partition)
{
    if (partition.m_type == TYPE_UNPARTITIONED)
        // Trying to set the type of a partition on a non-partitioned whole disk
        // device is a successful non-operation.
        return true;
    qDebug() << __FUNCTION__ << QString("set partition type on %1").arg(partition.getPath());
    //Set partition type appropriately for the type of file system stored in the partition.
    //  Libparted treats every type as a file system, except LVM which it treats as a flag.

    bool returnValue = false;

    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;
    if (getDeviceAndDisk(partition.m_devicePath, lpDevice, lpDisk)) {
        PedPartition *lpPartition = ped_disk_get_partition_by_sector(lpDisk, partition.getSector());
        if (lpPartition) {
            QString fsType = Utils::fileSystemTypeToString(partition.m_fstype);

            // Lookup libparted file system type using GParted's name, as most
            // match.  Exclude cleared as the name won't be recognised by
            // libparted and get_filesystem_string() has also translated it.
            PedFileSystemType *lpFsType = nullptr;
            if (partition.m_fstype != FS_CLEARED)
                lpFsType = ped_file_system_type_get(fsType.toLatin1());

            // If not found, and FS is udf, then try ntfs.
            // Actually MBR 07 IFS (Microsoft Installable File System) or
            // GPT BDP (Windows Basic Data Partition).
            // Ref: https://serverfault.com/a/829172
            if (!lpFsType && partition.m_fstype == FS_UDF)
                lpFsType = ped_file_system_type_get("ntfs");

            // default is Linux (83)
            if (!lpFsType)
                lpFsType = ped_file_system_type_get("ext2");

            bool supportsLvmFlag = ped_partition_is_flag_available(lpPartition, PED_PARTITION_LVM);

            if (lpFsType && partition.m_fstype != FS_LVM2_PV) {
                // Also clear any libparted LVM flag so that it doesn't
                // override the file system type
                if ((!supportsLvmFlag || ped_partition_set_flag(lpPartition, PED_PARTITION_LVM, 0)) && ped_partition_set_system(lpPartition, lpFsType) && commit(lpDisk)) {
                    qDebug() << __FUNCTION__ << QString("new partition type: %1").arg(lpPartition->fs_type->name);
                    returnValue = true;
                }
            } else if (partition.m_fstype == FS_LVM2_PV) {
                if (supportsLvmFlag && ped_partition_set_flag(lpPartition, PED_PARTITION_LVM, 1) && commit(lpDisk)) {
                    returnValue = true;
                } else if (!supportsLvmFlag) {
                    // Skip setting the lvm flag because the partition
                    // table type doesn't support it.  Applies to dvh
                    // and pc98 disk labels.
                    returnValue = true;
                }
            }
        }

        destroyDeviceAndDisk(lpDevice, lpDisk);
    }
    return returnValue;
}

bool PartedCore::createFileSystem(const Partition &partition)
{
    if (partition.m_fstype == FS_LUKS && partition.m_busy) {
        qDebug() << __FUNCTION__ << QString("partition contains open LUKS encryption for a create file system only step");
        return false;
    }
    qDebug() << __FUNCTION__ << partition.m_sectorsUsed << partition.m_sectorsUnused;
    qDebug() << __FUNCTION__ << QString("create new %1 file system").arg(partition.m_fstype);
    bool succes = false;
    FileSystem *pFilesystem = nullptr;
    switch (getFileSystem(partition.m_fstype).create) {
    case FS::NONE:
        break;
    case FS::GPARTED:
        break;
    case FS::LIBPARTED:
        break;
    case FS::EXTERNAL: {
        succes = (pFilesystem = getFileSystemObject(partition.m_fstype)) && pFilesystem->create(partition);
        if (succes && !partition.getFileSystemLabel().isEmpty()) {
            pFilesystem->writeLabel(partition);
        }
    }
    break;
    default:
        break;
    }
    return succes;
}

bool PartedCore::formatPartition(const Partition &partition)
{
    bool success = false;
    if (partition.m_fstype == FS_LUKS && partition.m_busy) {
        qDebug() << __FUNCTION__ << QString("partition contains open LUKS encryption for a format files system only step");
        return false;
    }

    if (partition.m_fstype == FS_CLEARED)
        success = eraseFilesystemSignatures(partition)
                   && setPartitionType(partition);
    else
        success = eraseFilesystemSignatures(partition)
                   && setPartitionType(partition)
                   && createFileSystem(partition);

    return success;
}

bool PartedCore::resize(const Partition &partitionNew)
{
    //ToDo fs linux-swap
    if (partitionNew.m_fstype == FS_LINUX_SWAP) {
        // linux-swap is recreated, not resized
        //        return    resizeMovePartition(partition_old, partition_new, operationdetail, true)
        //                  && recreate_linux_swap_filesystem(partition_new, operationdetail);
    }
    Sector delta = partitionNew.getSectorLength() - m_curpartition.getSectorLength();
    if (delta < 0LL) { // shrink
        //        return    check_repair_filesystem(partition_new)
        //                  && shrink_filesystem(curpartition, partition_new)
        //                  && resizeMovePartition(curpartition, partition_new, false);
    } else if (delta > 0LL) { // grow
        return checkRepairFileSystem(partitionNew)
               && resizeMovePartition(m_curpartition, partitionNew, true)
               && maxImizeFileSystem(partitionNew);
    }
    return true;
}

bool PartedCore::checkRepairFileSystem(const Partition &partition)
{
    if (partition.m_fstype == FS_LUKS && partition.m_busy) {
        qDebug() << "partition contains open LUKS encryption for a check file system only step";
        return false;
    }

    if (partition.m_busy)
        // Trying to check an online file system is a successful non-operation.
        return true;
    qDebug() << QString("PartedCore::checkRepairFileSystem:check file system on %1"
                        " for errors and (if possible) fix them")
             .arg(partition.getPath());

    bool succes = false;
    FileSystem *pFilesystem = nullptr;
    switch (getFileSystem(partition.m_fstype).check) {
    case FS::NONE:
        qDebug() << "PartedCore::checkRepairFileSystem ,checking is not available for this file system";
        break;
    case FS::GPARTED:
        break;
    case FS::LIBPARTED:
        break;
    case FS::EXTERNAL:
        succes = (pFilesystem = getFileSystemObject(partition.m_fstype)) && pFilesystem->checkRepair(partition);
        break;
    default:
        break;
    }

    return succes;
}

bool PartedCore::resizeMovePartition(const Partition &partitionOld, const Partition &partitionNew, bool rollbackOnFail)
{
    Action action = NONE;
    if (partitionNew.getSectorLength() > partitionOld.getSectorLength())
        action = GROW;
    else if (partitionNew.getSectorLength() < partitionOld.getSectorLength())
        action = SHRINK;

    if (partitionNew.m_sectorStart > partitionOld.m_sectorStart && partitionNew.m_sectorEnd > partitionOld.m_sectorEnd)
        action = action == GROW ? MOVE_RIGHT_GROW : action == SHRINK ? MOVE_RIGHT_SHRINK : MOVE_RIGHT;
    else if (partitionNew.m_sectorStart < partitionOld.m_sectorStart && partitionNew.m_sectorEnd < partitionOld.m_sectorEnd)
        action = action == GROW ? MOVE_LEFT_GROW : action == SHRINK ? MOVE_LEFT_SHRINK : MOVE_LEFT;

    Sector newStart = -1;
    Sector newEnd = -1;
    bool success = resizeMovePartitionImplement(partitionOld, partitionNew, newStart, newEnd);
    if (!success && rollbackOnFail) {
        Partition *partitionIntersection = partitionNew.clone();
        partitionIntersection->m_sectorStart = std::max(partitionOld.m_sectorStart,
                                                        partitionNew.m_sectorStart);
        partitionIntersection->m_sectorEnd = std::min(partitionOld.m_sectorEnd,
                                                      partitionNew.m_sectorEnd);

        Partition *partitionRestore = partitionOld.clone();
        // Ensure that old partition boundaries are not modified
        partitionRestore->m_alignment = ALIGN_STRICT;

        resizeMovePartitionImplement(*partitionIntersection, *partitionRestore, newStart, newEnd);
        delete partitionRestore;
        partitionRestore = nullptr;
        delete partitionIntersection;
        partitionIntersection = nullptr;
    }

    return success;
}

bool PartedCore::resizeMovePartitionImplement(const Partition &partitionOld, const Partition &partitionNew, Sector &newStart, Sector &newEnd)
{
    bool success = false;
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;
    if (getDeviceAndDisk(partitionOld.m_devicePath, lpDevice, lpDisk)) {
        PedPartition *lpPartition = getLpPartition(lpDisk, partitionOld);
        if (lpPartition) {
            PedConstraint *constraint = nullptr;
            if (partitionNew.m_alignment == ALIGN_STRICT || partitionNew.m_alignment == ALIGN_MEBIBYTE || partitionNew.m_strictStart) {
                PedGeometry *geom = ped_geometry_new(lpDevice,
                                                     partitionNew.m_sectorStart,
                                                     partitionNew.getSectorLength());
                if (geom) {
                    constraint = ped_constraint_exact(geom);
                    ped_geometry_destroy(geom);
                }
            } else {
                constraint = ped_constraint_any(lpDevice);
            }

            if (constraint) {
                if (ped_disk_set_partition_geom(lpDisk,
                                                lpPartition,
                                                constraint,
                                                partitionNew.m_sectorStart,
                                                partitionNew.m_sectorEnd)) {
                    newStart = lpPartition->geom.start;
                    newEnd = lpPartition->geom.end;

                    success = commit(lpDisk);
                }

                ped_constraint_destroy(constraint);
            }
        }
        destroyDeviceAndDisk(lpDevice, lpDisk);
    }

    return success;
}

bool PartedCore::maxImizeFileSystem(const Partition &partition)
{
    if (partition.m_fstype == FS_LUKS && partition.m_busy) {
        qDebug() << "PartedCore::maxImizeFileSystem: partition contains open LUKS encryption for a maximize file system only step";
        return false;
    }
    qDebug() << "PartedCore::maxImizeFileSystem: grow file system to fill the partition";

    // Checking if growing is available or allowed is only relevant for the check
    // repair operation to inform the user why the grow step is being skipped.  For a
    // resize/move operation these growing checks are merely retesting those performed
    // to allow the operation to be queued in the first place.  See
    // Win_GParted::set_valid_operations() and
    // Dialog_Partition_Resize_Move::Resize_Move_Normal().
    if (getFileSystem(partition.m_fstype).grow == FS::NONE) {
        qDebug() << "PartedCore::maxImizeFileSystem:growing is not available for this file system";
        return true;
    }
    bool success = resizeFileSystemImplement(partition, partition);

    return success;
}

bool PartedCore::resizeFileSystemImplement(const Partition &partitionOld, const Partition &partitionNew)
{
    bool fillPartition = false;
    const FS &fsCap = getFileSystem(partitionNew.m_fstype);
    FS::Support action = FS::NONE;
    if (partitionNew.getSectorLength() >= partitionOld.getSectorLength()) {
        // grow (always maximises the file system to fill the partition)
        fillPartition = true;
        action = (partitionOld.m_busy) ? fsCap.online_grow : fsCap.grow;
    } else {
        // shrink
        fillPartition = false;
        action = (partitionOld.m_busy) ? fsCap.online_shrink : fsCap.shrink;
    }
    bool success = false;
    FileSystem *pFilesystem = nullptr;
    switch (action) {
    case FS::NONE:
        break;
    case FS::GPARTED:
        break;
    case FS::LIBPARTED:
        success = resizeMoveFileSystemUsingLibparted(partitionOld, partitionNew);
        break;
    case FS::EXTERNAL:
        success = (pFilesystem = getFileSystemObject(partitionNew.m_fstype)) && pFilesystem->resize(partitionNew, fillPartition);
        break;
    default:
        break;
    }

    return success;
}

bool PartedCore::resizeMoveFileSystemUsingLibparted(const Partition &partitionOld, const Partition &partitionNew)
{
    bool returnValue = false;
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;
    if (getDeviceAndDisk(partitionOld.m_devicePath, lpDevice, lpDisk)) {
        PedFileSystem *fs = nullptr;
        PedGeometry *lpGeom = nullptr;

        lpGeom = ped_geometry_new(lpDevice,
                                   partitionOld.m_sectorStart,
                                   partitionOld.getSectorLength());
        if (lpGeom) {
            fs = ped_file_system_open(lpGeom);

            ped_geometry_destroy(lpGeom);
            lpGeom = nullptr;

            if (fs) {
                lpGeom = ped_geometry_new(lpDevice,
                                           partitionNew.m_sectorStart,
                                           partitionNew.getSectorLength());
                if (lpGeom) {
                    if (returnValue)
                        commit(lpDisk);

                    ped_geometry_destroy(lpGeom);
                }
                ped_file_system_close(fs);
            }
        }
        destroyDeviceAndDisk(lpDevice, lpDisk);
    }

    return returnValue;
}

void PartedCore::onRefreshDeviceInfo()
{
    probeDeviceInfo();
    emit updateDeviceInfo(m_inforesult);
}

bool PartedCore::mountAndWriteFstab(const QString &mountpath)
{
    qDebug() << __FUNCTION__ << "Mount start";
    bool success = true;
    QString output, errstr;
    QString cmd ;
    QString type = Utils::fileSystemTypeToString(m_curpartition.m_fstype);
    QString partitionPath = m_curpartition.getPath();
//    qDebug() << partitionPath << type << "111111111111";
//    if (mount(partitionPath.toStdString().c_str(), mountpath.toStdString().c_str(), type.toStdString().c_str(), 0, nullptr) != 0) {
//        bool success = false;
//        qDebug() << "111111111111";
//        emit refreshDeviceInfo();
//        return success;
//    }
//    qDebug() << __FUNCTION__ << "Mount success";
    if (type == FS_FAT32 || type == FS_FAT16) {
        cmd = QString("mount -v %1 %2 -o -o dmask=000,fmask=111").arg(partitionPath).arg(mountpath);
    } else if (type == FS_HFS) {
        cmd = QString("mount -v %1 %2 -o -o dir_umask=000,file_umask=111").arg(partitionPath).arg(mountpath);
    } else {
        cmd = QString("mount -v %1 %2").arg(partitionPath).arg(mountpath);
    }

    int exitcode = Utils::executCmd(cmd, output, errstr);
    if (exitcode != 0) {
        // QString type = Utils::get_filesystem_kernel_name(curpartition.fstype);
        // cmd = QString("mount -v -t %1 %2 %3").arg("").arg(partitionpath).arg(mountpath);
        success = false;

        emit refreshDeviceInfo();

        return success;
    }

    //永久挂载
    qDebug() << __FUNCTION__ << "Permanent mount start";
    QFile file("/etc/fstab");
    QString displayString;
    QStringList list;

    if (!file.open(QIODevice::ReadOnly)) { //打开指定文件
        qDebug() << __FUNCTION__ << "Permanent mount open file error";
        success = false;
        emit refreshDeviceInfo();
        return success;
    } else {
        while (!file.atEnd()) {
            QByteArray line = file.readLine();//获取数据
            qDebug() << line;

            if(line.contains(m_curpartition.m_uuid.toStdString().c_str())) {
                QString str = QString("UUID=%1 %2 %3 defaults,nofail 0 0\n").arg(m_curpartition.m_uuid).arg(mountpath).arg(type);
                list << str;
                break;
            } else if (file.atEnd()) {
                QString str = QString("UUID=%1 %2 %3 defaults,nofail 0 0\n").arg(m_curpartition.m_uuid).arg(mountpath).arg(type);
                list << line;
                list << str;
                break;
            }
            list << line;
        }
        file.close();
        if (file.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
            QTextStream out(&file);

            for (int i = 0; i < list.count(); i++) {
                out << list.at(i);
                out.flush();
            }
            file.close();
            qDebug() << __FUNCTION__ << "Permanent mount end";
        } else {
            success = false;
            emit refreshDeviceInfo();
            qDebug() << __FUNCTION__ << "Permanent mount open file error";
            return success;
        }
    }

    emit refreshDeviceInfo();

    return success;
}

bool PartedCore::unmount()
{
    qDebug() << __FUNCTION__ << "Unmount start";
    //永久卸载
    bool success = true;
    qDebug() << __FUNCTION__ << "Permanent unmount start";
    QString partitionUuid = m_curpartition.m_uuid;
    QFile file("/etc/fstab");
    QString displayString;

    if (!file.open(QIODevice::ReadOnly)) { //打开指定文件
        qDebug() << __FUNCTION__ << "Permanent unmount open file error";
        success = false;
        emit refreshDeviceInfo();
        return success;
    } else {
        QStringList list;
        while (!file.atEnd()) {
            QByteArray line = file.readLine();//获取数据
            QString str = line;

            if (str.contains(partitionUuid)) {
                continue;
            }
            list << str;
        }
        file.close();
        if (file.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
            QTextStream out(&file);
            for (int i = 0; i < list.count(); i++) {
                out << list.at(i);
                out.flush();
            }
            file.close();
        } else {
            qDebug() << __FUNCTION__ << "Permanent unmount open file error";
            success = false;
            emit refreshDeviceInfo();
            return success;
        }
    }

    QString output, errstr;
    QVector<QString> mountpoints = m_curpartition.getMountPoints();
    for (QString path : mountpoints) {
        QString cmd = QString("umount -v %1").arg(path);
        int exitcode = Utils::executCmd(cmd, output, errstr);
        if (0 != exitcode) {
            success = false;
            emit refreshDeviceInfo();
            return success;
        }
//        if (umount(path.toStdString().c_str()) != 0) {
//            success = false;
//            emit refreshDeviceInfo();
//            return success;
//        }
    }

    emit refreshDeviceInfo();

    qDebug() << __FUNCTION__ << "Unmount end";

    return success;
}

bool PartedCore::create(const PartitionVec &infovec)
{
    qDebug() << __FUNCTION__ << "Create start";
    bool success = true;
    for (PartitionInfo info : infovec) {
        Partition newPartition;
        newPartition.reset(info);
        if (!create(newPartition)) {
            qDebug() << __FUNCTION__ << "Create Patitione error";
            success = false;
            break;
        }
    }
    emit refreshDeviceInfo();
    qDebug() << __FUNCTION__ << "Create end";
    return success;
}

bool PartedCore::create(Partition &newPartition)
{
    bool success = false;
    if (newPartition.m_type == TYPE_EXTENDED) {
        success = createPartition(newPartition);
    } else {
        FS_Limits fsLimits = getFileSystemLimits(newPartition.m_fstype, newPartition);
        success = createPartition(newPartition, fsLimits.min_size / newPartition.m_sectorSize);
    }
    if (!success)
        return false;

    if (!newPartition.m_name.isEmpty()) {
        if (!namePartition(newPartition))
            return false;
    }

    if (newPartition.m_type == TYPE_EXTENDED || newPartition.m_fstype == FS_UNFORMATTED)
        return true;
    else if (newPartition.m_fstype == FS_CLEARED)
        return eraseFilesystemSignatures(newPartition);
    else
        return eraseFilesystemSignatures(newPartition)
               && setPartitionType(newPartition)
               && createFileSystem(newPartition);

}

bool PartedCore::createPartition(Partition &newPartition, Sector minSize)
{
    qDebug() << __FUNCTION__ << "create empty partition";
    newPartition.m_partitionNumber = 0;
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;
    if (getDeviceAndDisk(newPartition.m_devicePath, lpDevice, lpDisk)) {
        PedPartitionType type;
        PedConstraint *constraint = nullptr;
        PedFileSystemType *fsType = nullptr;
        //create new partition
        switch (newPartition.m_type) {
        case TYPE_PRIMARY:
            type = PED_PARTITION_NORMAL;
            break;
        case TYPE_LOGICAL:
            type = PED_PARTITION_LOGICAL;
            break;
        case TYPE_EXTENDED:
            type = PED_PARTITION_EXTENDED;
            break;

        default:
            type = PED_PARTITION_FREESPACE;
        }
        if (newPartition.m_type != TYPE_EXTENDED)
            fsType = ped_file_system_type_get("ext2");

        PedPartition *lpPartition = ped_partition_new(lpDisk, type, fsType,
                                                       newPartition.m_sectorStart, newPartition.m_sectorEnd);

        if (lpPartition) {
            if (newPartition.m_alignment == ALIGN_STRICT
                    || newPartition.m_alignment == ALIGN_MEBIBYTE) {
                PedGeometry *geom = ped_geometry_new(lpDevice, newPartition.m_sectorStart, newPartition.getSectorLength());
                if (geom) {
                    constraint = ped_constraint_exact(geom);
                    ped_geometry_destroy(geom);
                }
            } else
                constraint = ped_constraint_any(lpDevice);

            if (constraint) {
                if (minSize > 0 && newPartition.m_fstype != FS_XFS) // Permit copying to smaller xfs partition
                    constraint->min_size = minSize;

                if (ped_disk_add_partition(lpDisk, lpPartition, constraint) && commit(lpDisk)) {
                    newPartition.setPath(getPartitionPath(lpPartition));

                    newPartition.m_partitionNumber = lpPartition->num;
                    newPartition.m_sectorStart = lpPartition->geom.start;
                    newPartition.m_sectorEnd = lpPartition->geom.end;
                }
                ped_constraint_destroy(constraint);
            }
        }
        destroyDeviceAndDisk(lpDevice, lpDisk);
    }
    bool bsucces = newPartition.m_partitionNumber > 0;

    return bsucces;
}

bool PartedCore::format(const QString &fstype, const QString &name)
{
    qDebug() << __FUNCTION__ << "Format Patitione start";
    Partition part = m_curpartition;
    part.m_fstype = Utils::stringToFileSystemType(fstype);
    part.setFilesystemLabel(name);
    bool success = formatPartition(part);
    emit refreshDeviceInfo();
    qDebug() << __FUNCTION__ << "Format Patitione end";
    return success;
}

bool PartedCore::resize(const PartitionInfo &info)
{
    qDebug() << __FUNCTION__ << "Resize Patitione start";
    Partition newPartition = m_curpartition;
    newPartition.reset(info);
    bool success = resize(newPartition);
    emit refreshDeviceInfo();
    qDebug() << __FUNCTION__ << "Resize Patitione end";
    return success;
}

QStringList PartedCore::getallsupportfs()
{
    return m_supportedFileSystems->getAllFsName();
}

HardDiskInfo PartedCore::getDeviceHardInfo(const QString &devicepath)
{
    qDebug() << __FUNCTION__ << "Get Device Hard Info Start";
    HardDiskInfo hdinfo;
    if (devicepath.isEmpty()) {
        qDebug() << "disk path is empty";
        return hdinfo;
    }

    DeviceStorage device;
    device.getDiskInfoFromHwinfo(devicepath);

    device.getDiskInfoFromLshw(devicepath);

    device.getDiskInfoFromLsblk(devicepath);

    device.getDiskInfoFromSmartCtl(devicepath);

    hdinfo.m_Model = device.m_Model;
    hdinfo.m_Vendor = device.m_Vendor;
    hdinfo.m_MediaType = device.m_MediaType;
    hdinfo.m_Size = device.m_Size;
    hdinfo.m_RotationRate = device.m_RotationRate;
    hdinfo.m_Interface = device.m_Interface;
    hdinfo.m_SerialNumber = device.m_SerialNumber;
    hdinfo.m_Version = device.m_Version;
    hdinfo.m_Capabilities = device.m_Capabilities;
    hdinfo.m_Description = device.m_Description;
    hdinfo.m_PowerOnHours = device.m_PowerOnHours;
    hdinfo.m_PowerCycleCount = device.m_PowerCycleCount;
    hdinfo.m_FirmwareVersion = device.m_FirmwareVersion;
    hdinfo.m_Speed = device.m_Speed;

    qDebug() << __FUNCTION__ << "Get Device Hard Info end";
    return hdinfo;
}

DeviceInfo PartedCore::getDeviceinfo()
{
    DeviceInfo info;
//    qDebug() << __FUNCTION__ << "#########";
//    qDebug() << info.m_path << info.heads << info.cylinders << info.serial_number << info.max_prims;
    return info;
}

DeviceInfoMap PartedCore::getAllDeviceinfo()
{
    return m_inforesult;
}

void PartedCore::setCurSelect(const PartitionInfo &info)
{
    bool bfind = false;
    for (auto it = m_devicemap.begin(); it != m_devicemap.end() && !bfind; it++) {
        if (it.key() == info.m_devicePath) {
            Device dev = it.value();
            for (auto itpart = dev.m_partitions.begin(); itpart != dev.m_partitions.end() && !bfind; itpart++) {
                Partition part = *(*itpart);
                if (infoBelongToPartition(part, info)) {
                    m_curpartition = part;
                    bfind = true;
                }
                if (part.m_insideExtended && !bfind) {
                    for (auto itextend = part.m_logicals.begin(); itextend != part.m_logicals.end(); itextend++) {
                        Partition partlogical = *(*itextend);
                        if (infoBelongToPartition(partlogical, info)) {
                            m_curpartition = partlogical;
                            bfind = true;
                        }
                    }
                }
            }
        }
    }
}

QString PartedCore::getDeviceHardStatus(const QString &devicepath)
{
    qDebug() << __FUNCTION__ << "Get Device Hard Status Start";
    QString status;
    QString devicePath = devicepath;
    if (devicepath.contains("nvme")) {
        QStringList list = devicepath.split("nvme");
        if (list.size() < 2) {
            return status;
        }
        QString str = "";
        for (int i = 0; i < list.at(1).size(); i++) {
            if(list.at(1).at(i) >= "0" && list.at(1).at(i) <= "9"){
                str += list.at(1).at(i);
            }else {
                break;
            }
        }

        devicePath = list.at(0) + "nvme" + str;
//        qDebug() << devicePath << "1111111111111111" << endl;
    }

    QString cmd = QString("smartctl -H %1").arg(devicePath);
    QProcess proc;
    proc.start(cmd);
    proc.waitForFinished(-1);
    QString output = proc.readAllStandardOutput();

    if (output.indexOf("SMART overall-health self-assessment test result:") != -1) {
        QStringList list = output.split("\n");
        for (int i = 0;i < list.size(); i++) {
            if (list.at(i).indexOf("SMART overall-health self-assessment test result:") != -1) {
                status = list.at(i).mid(strlen("SMART overall-health self-assessment test result:"));
                status.remove(QRegExp("^ +\\s*"));
//                qDebug() << __FUNCTION__ << status;
                break;
            }
        }
    } else {
        QString cmd = QString("smartctl -H -d sat %1").arg(devicePath);
        QProcess proc;
        proc.start(cmd);
        proc.waitForFinished(-1);
        QString output = proc.readAllStandardOutput();

        if (output.indexOf("SMART overall-health self-assessment test result:") != -1) {
            QStringList list = output.split("\n");
            for (int i = 0;i < list.size(); i++) {
                if (list.at(i).indexOf("SMART overall-health self-assessment test result:") != -1) {
                    status = list.at(i).mid(strlen("SMART overall-health self-assessment test result:"));
                    status.remove(QRegExp("^ +\\s*"));
//                    qDebug() << __FUNCTION__ << status;
                    break;
                }
            }
        }
    }

    qDebug() << __FUNCTION__ << "Get Device Hard Status End";
    return status;
}

HardDiskStatusInfoList PartedCore::getDeviceHardStatusInfo(const QString &devicepath)
{
    qDebug() << __FUNCTION__ << "Get Device Hard Status Info Start";
    HardDiskStatusInfoList hdsilist;

    QString devicePath = devicepath;
//    QString devicePath = "/dev/nvme12n1";
    if (devicePath.contains("nvme")) {
        //重新拼接硬盘字符串
        QStringList list = devicePath.split("nvme");
        if (list.size() < 2) {
            return hdsilist;
        }
        QString str = "";
        for (int i = 0; i < list.at(1).size(); i++) {
            if(list.at(1).at(i) >= "0" && list.at(1).at(i) <= "9"){
                str += list.at(1).at(i);
            }else {
                break;
            }
        }

        devicePath = list.at(0) + "nvme" + str;

        QString cmd = QString("smartctl -A %1").arg(devicePath);
        QProcess proc;
        proc.start(cmd);
        proc.waitForFinished(-1);
        QString output = proc.readAllStandardOutput();
//        QString output = "smartctl 6.6 2017-11-05 r4594 [x86_64-linux-4.19.0-6-amd64] (local build)\nCopyright (C) 2002-17, Bruce Allen, Christian Franke, www.smartmontools.org\n\n=== START OF SMART DATA SECTION ===\nSMART/Health Information (NVMe Log 0x02, NSID 0xffffffff)\nCritical Warning:                   0x00\nTemperature:                        25 Celsius\nAvailable Spare:                    100%\nAvailable Spare Threshold:          5%\nPercentage Used:                    1%\nData Units Read:                    3,196,293 [1.63 TB]\nData Units Written:                 3,708,861 [1.89 TB]\nHost Read Commands:                 47,399,157\nHost Write Commands:                65,181,192\nController Busy Time:               418\nPower Cycles:                       97\nPower On Hours:                     1,362\nUnsafe Shutdowns:                   44\nMedia and Data Integrity Errors:    0\nError Information Log Entries:      171\nWarning  Comp. Temperature Time:    0\nCritical Comp. Temperature Time:    0\n\n";
        list.clear();
        list = output.split("\n");
        for (int i = 0; i < list.size(); i++) {
            HardDiskStatusInfo hdsinfo;
            if (list.at(i).contains(":")) {
                QStringList slist = list.at(i).split(":");
                if (slist.size() != 2) {
                    break;
                }
                hdsinfo.m_attributeName = slist.at(0);
                hdsinfo.m_rawValue = slist.at(1).trimmed();
            } else {
                continue;
            }
            hdsilist.append(hdsinfo);
        }
    } else {

        QString cmd = QString("smartctl -A %1").arg(devicepath);
        QProcess proc;
        proc.start(cmd);
        proc.waitForFinished(-1);
        QString output = proc.readAllStandardOutput();

        if (output.contains("ID# ATTRIBUTE_NAME          FLAG     VALUE WORST THRESH TYPE      UPDATED  WHEN_FAILED RAW_VALUE")) {
            QStringList list = output.split("\n");
            int n = list.indexOf("ID# ATTRIBUTE_NAME          FLAG     VALUE WORST THRESH TYPE      UPDATED  WHEN_FAILED RAW_VALUE");
            for (int i = n+1;i < list.size(); i++) {
                HardDiskStatusInfo hdsinfo;
                QString statusInfo = list.at(i);


                QStringList slist = statusInfo.split(' ');
                slist.removeAll("");

                if (slist.size() == 0) {
                    break;
                }

                if (list.size() >= 10) {
                    hdsinfo.m_id = slist.at(0);
                    hdsinfo.m_attributeName = slist.at(1);
                    hdsinfo.m_flag = slist.at(2);
                    hdsinfo.m_value = slist.at(3);
                    hdsinfo.m_worst = slist.at(4);
                    hdsinfo.m_thresh = slist.at(5);
                    hdsinfo.m_type = slist.at(6);
                    hdsinfo.m_updated = slist.at(7);
                    hdsinfo.m_whenFailed = slist.at(8);
                    for(int k = 9; k < slist.size(); k++) {
                        hdsinfo.m_rawValue += slist.at(k);
                    }
                }

                hdsilist.append(hdsinfo);
            }
        } else {
            QString cmd = QString("smartctl -A -d sat %1").arg(devicepath);
            QProcess proc;
            proc.start(cmd);
            proc.waitForFinished(-1);
            QString output = proc.readAllStandardOutput();
            if (output.contains("ID# ATTRIBUTE_NAME          FLAG     VALUE WORST THRESH TYPE      UPDATED  WHEN_FAILED RAW_VALUE")) {
                QStringList list = output.split("\n");
                int n = list.indexOf("ID# ATTRIBUTE_NAME          FLAG     VALUE WORST THRESH TYPE      UPDATED  WHEN_FAILED RAW_VALUE");
                for (int i = n+1;i < list.size(); i++) {
                    HardDiskStatusInfo hdsinfo;
                    QString statusInfo = list.at(i);
                    QStringList slist = statusInfo.split(' ');
                    slist.removeAll("");

                    if (slist.size() == 0) {
                        break;
                    }

                    if (list.size() >= 10) {
                        hdsinfo.m_id = slist.at(0);
                        hdsinfo.m_attributeName = slist.at(1);
                        hdsinfo.m_flag = slist.at(2);
                        hdsinfo.m_value = slist.at(3);
                        hdsinfo.m_worst = slist.at(4);
                        hdsinfo.m_thresh = slist.at(5);
                        hdsinfo.m_type = slist.at(6);
                        hdsinfo.m_updated = slist.at(7);
                        hdsinfo.m_whenFailed = slist.at(8);
                        for(int k = 9; k < slist.size(); k++) {
                            hdsinfo.m_rawValue += slist.at(k);
                        }
                    }

                    hdsilist.append(hdsinfo);
                }
            }
        }
    }
    qDebug() << __FUNCTION__ << "Get Device Hard Status Info end";
    return hdsilist;
}
bool PartedCore::deletePartition()
{
    qDebug() << __FUNCTION__ << "Delete Partition start";
    PedPartition *ped = nullptr;
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;

    QString parttitionPath = m_curpartition.getPath();
    QString devicePath = m_curpartition.m_devicePath;

    if (!getDeviceAndDisk(devicePath, lpDevice, lpDisk)) {
        qDebug() << __FUNCTION__ << "Delete Partition get device and disk failed";

        emit refreshDeviceInfo();
        emit deletePatition("0:1");

        return false;
    }

    QStringList list;

    for (int i=parttitionPath.size()-1;i!=0;i--) {
        if (parttitionPath.at(i) >= '0' && parttitionPath.at(i) <= '9') {
                list.insert(0, parttitionPath.at(i));
        } else {
            break;
        }
    }

    int num = list.join("").toInt();
    ped = ped_disk_get_partition(lpDisk, num);

    if (ped == nullptr) {
        qDebug() << __FUNCTION__ << "Delete Partition Get Partition failed";

        emit refreshDeviceInfo();
        emit deletePatition("0:2");

        return false;
    }

    int i = ped_disk_delete_partition(lpDisk, ped);

    if (i == 0) {
        qDebug() << __FUNCTION__ << "Delete Partition failed";

        emit refreshDeviceInfo();
        emit deletePatition("0:3");

        return false;
    }

    if (!commit(lpDisk)) {
        qDebug() << __FUNCTION__ << "Delete Partition commit failed";

        emit refreshDeviceInfo();
        emit deletePatition("0:4");

        return false;
    }

    destroyDeviceAndDisk(lpDevice, lpDisk);

    emit refreshDeviceInfo();
    emit deletePatition("1:0");

    qDebug() << __FUNCTION__ << "Delete Partition end";
    return true;
}

bool PartedCore::hidePartition()
{
    qDebug() << __FUNCTION__ << "Hide Partition start";
    PedPartitionFlag flag = PED_PARTITION_HIDDEN;
    PedPartition *ped = nullptr;
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;

    QString parttitionPath = m_curpartition.getPath();
    QString devicePath = m_curpartition.m_devicePath;

    if (!getDeviceAndDisk(devicePath, lpDevice, lpDisk)) {
        qDebug() << __FUNCTION__ << "Hide Partition get device and disk failed";

        emit refreshDeviceInfo();
        emit hidePartitionInfo("0");

        return false;
    }

    QStringList list;

    for (int i=parttitionPath.size()-1;i!=0;i--) {
        if (parttitionPath.at(i) >= '0' && parttitionPath.at(i) <= '9') {
                list.insert(0, parttitionPath.at(i));
        } else {
            break;
        }
    }

    int num = list.join("").toInt();
    ped = ped_disk_get_partition(lpDisk, num);

    if (ped == nullptr) {
        qDebug() << __FUNCTION__ << "Hide Partition get partition failed";

        emit refreshDeviceInfo();
        emit hidePartitionInfo("0");

        return false;
    }

    int hideenFlag = ped_partition_get_flag(ped, PED_PARTITION_HIDDEN);

    if (hideenFlag == 1) {
        qDebug() << __FUNCTION__ << "Hide Partition set partition failed";

        emit refreshDeviceInfo();
        emit hidePartitionInfo("0");

        return false;
    }

    if (ped_partition_set_flag(ped, flag, 1) && commit(lpDisk)) {
        qDebug() << __FUNCTION__ << "hide partition success";

        destroyDeviceAndDisk(lpDevice, lpDisk);

        emit refreshDeviceInfo();
        emit hidePartitionInfo("1");

        qDebug() << __FUNCTION__ << "Hide Partition end";
        return true;
    } else {
        qDebug() << __FUNCTION__ << "hide partition failed";

        destroyDeviceAndDisk(lpDevice, lpDisk);

        emit refreshDeviceInfo();
        emit hidePartitionInfo("0");

        qDebug() << __FUNCTION__ << "Hide Partition end";
        return false;
    }
}

bool PartedCore::showPartition()
{
    qDebug() << __FUNCTION__ << "Show Partition start";
    PedPartitionFlag flag = PED_PARTITION_HIDDEN;
    PedPartition *ped = nullptr;
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;

    QString parttitionPath = m_curpartition.getPath();
    QString devicePath = m_curpartition.m_devicePath;

    if (!getDeviceAndDisk(devicePath, lpDevice, lpDisk)) {
        qDebug() << __FUNCTION__ << "Show Partition get device and disk failed";

        emit refreshDeviceInfo();
        emit showPartitionInfo("0");

        return false;
    }

    QStringList list;

    for (int i=parttitionPath.size()-1;i!=0;i--) {
        if (parttitionPath.at(i) >= '0' && parttitionPath.at(i) <= '9') {
                list.insert(0, parttitionPath.at(i));
        } else {
            break;
        }
    }

    int num = list.join("").toInt();
    ped = ped_disk_get_partition(lpDisk, num);

    if (ped == nullptr) {
        qDebug() << __FUNCTION__ << "Show Partition get partition failed";

        emit refreshDeviceInfo();
        emit showPartitionInfo("0");

        return false;
    }

    int hideenFlag = ped_partition_get_flag(ped, PED_PARTITION_HIDDEN);

    if (hideenFlag == 0) {
        qDebug() << __FUNCTION__ << "Show Partition get partition failed";

        emit refreshDeviceInfo();
        emit showPartitionInfo("0");

        return false;
    }

    if (ped_partition_set_flag(ped, flag, 0) && commit(lpDisk)) {
        qDebug() << __FUNCTION__ << "Show Partition success";
        destroyDeviceAndDisk(lpDevice, lpDisk);

        emit refreshDeviceInfo();
        emit showPartitionInfo("1");

        qDebug() << __FUNCTION__ << "Show Partition end";
        return true;
    } else {
        qDebug() << __FUNCTION__ << "Show Partition failed";

        destroyDeviceAndDisk(lpDevice, lpDisk);

        emit refreshDeviceInfo();
        emit showPartitionInfo("0");

        qDebug() << __FUNCTION__ << "Show Partition end";
        return false;
    }
}

int PartedCore::getPartitionHiddenFlag(const QString &devicePath, const QString &parttitionPath)
{
    qDebug() << __FUNCTION__ << "Get Partition Hidden Flag start";
    PedPartition *ped = nullptr;
    PedDevice *lpDevice = nullptr;
    PedDisk *lpDisk = nullptr;

    if (!getDeviceAndDisk(devicePath, lpDevice, lpDisk)) {
        qDebug() << __FUNCTION__ << "Get Partition Hidden Flag get device and disk failed";
        return -1;
    }

    ped = ped_disk_get_partition(lpDisk, parttitionPath.right(1).toInt());

    if (ped == nullptr) {
        qDebug() << __FUNCTION__ << "Get Partition Hidden Flag get partition failed";
        return -1;
    }

    int hideenFlag = ped_partition_get_flag(ped, PED_PARTITION_HIDDEN);

    destroyDeviceAndDisk(lpDevice, lpDisk);

    qDebug() << __FUNCTION__ << "Get Partition Hidden Flag end";
    return hideenFlag;
}

bool PartedCore::detectionPartitionTableError(const QString &devicePath)
{
    qDebug() << __FUNCTION__ << "Detection Partition Table Error start";
    QString cmd = QString("fdisk -l %1").arg(devicePath);
    FILE *fd = nullptr;
    fd = popen(cmd.toStdString().data(), "r");
    char pb[1024];
    memset(pb, 0, 1024);

    if (fd == nullptr) {
        qDebug() << __FUNCTION__ << "Detection Partition Table Error order error";
        return false;
    }

    while (fgets(pb, 1024, fd) != nullptr) {
        if (strstr(pb, "Partition table entries are not in disk order") != nullptr) {
            return true;
        }
    }

    qDebug() << __FUNCTION__ << "Detection Partition Table Error end";
    return false;
}

bool PartedCore::updateUsb()
{
    qDebug() << __FUNCTION__ << "USB add update start";

    sleep(5);
//    onRefreshDeviceInfo();
//    autoMount();
    emit usbUpdated();

    autoMount();

    qDebug() << __FUNCTION__ << "USB add update end";
    return true;
}

bool PartedCore::updateUsbRemove()
{
    qDebug() << __FUNCTION__ << "USB add update remove"; 

    emit refreshDeviceInfo();
    emit usbUpdated();

    autoUmount();

    qDebug() << __FUNCTION__ << "USB add update end";
    return true;
}

void PartedCore::autoMount()
{
    //因为永久挂载的原因需要先执行mount -a让系统文件挂载生效
    qDebug() << __FUNCTION__ << "solt automount start";
    QString output,errstr;
    QString cmd = QString("mount -a");
    int exitcode = Utils::executCmd(cmd, output, errstr);

    if (exitcode != 0) {
        qDebug() << __FUNCTION__ << output;
    }

    emit refreshDeviceInfo();

    qDebug() << __FUNCTION__ << "solt automount end";
}

void PartedCore::autoUmount()
{
    qDebug() << __FUNCTION__ << "autoUmount start";
    QStringList deviceList;

    for (auto it = m_inforesult.begin();it != m_inforesult.end(); it++) {
        deviceList << it.key();
    }

    QString cmd = QString("df");
    FILE *fd = nullptr;
    fd = popen(cmd.toStdString().data(), "r");
    char pb[1024];
    memset(pb, 0, 1024);

//    if (fd == nullptr) {
//        qDebug() << __FUNCTION__ <<"exeuted cmd failed";
//    }

    while (fgets(pb, 1024, fd) != nullptr) {
        QString dfBuf = pb;
        //qDebug() << pb << "qqqqqqqqqqqqqqqqqqqqqqqqqqq";
        QStringList dfList = dfBuf.split(" ");
        if (deviceList.indexOf(dfList.at(0).left(dfList.at(0).size()-1)) == -1 && dfList.at(0).contains("/dev/")) {
            cmd = QString("umount -v %1").arg(dfList.last());
            QString output, errstr;
            int exitcode = Utils::executCmd(cmd, output, errstr);
            if (exitcode != 0) {
                qDebug() << __FUNCTION__ << "卸载挂载点失败";
            }
        }
    }

    qDebug() << __FUNCTION__ << "autoUmount end";
}

int PartedCore::test()
{
    return 1;
}

} // namespace DiskManager
