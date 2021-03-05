/**
 * @copyright 2020-2020 Uniontech Technology Co., Ltd.
 *
 * @file diskhealthdetectiondialog.cpp
 *
 * @brief 磁盘健康检测页面展示类
 *
 * @date 2020-08-19 17:08
 *
 * Author: yuandandan  <yuandandan@uniontech.com>
 *
 * Maintainer: yuandandan  <yuandandan@uniontech.com>
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
#include "diskhealthdetectiondialog.h"
#include "common.h"
#include "diskhealthdetectiondelegate.h"
#include "partedproxy/dmdbushandler.h"
#include "messagebox.h"
#include "diskhealthheaderview.h"

#include <DFrame>
#include <DGuiApplicationHelper>
#include <DMessageManager>
#include <DFontSizeManager>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QTableWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>

DiskHealthDetectionDialog::DiskHealthDetectionDialog(const QString &devicePath, HardDiskStatusInfoList hardDiskStatusInfoList, QWidget *parent)
    : DDialog(parent)
    , m_devicePath(devicePath)
    , m_hardDiskStatusInfoList(hardDiskStatusInfoList)
{
    initUI();
    initConnections();
}

DiskHealthDetectionDialog::~DiskHealthDetectionDialog()
{
    delete m_diskHealthDetectionDelegate;
}

void DiskHealthDetectionDialog::initUI()
{
    setIcon(QIcon::fromTheme(appName));
    setTitle(tr("Check Health")); // 硬盘健康检测
    setMinimumSize(726, 676);

    QIcon iconDisk = Common::getIcon("disk");
    DLabel *diskLabel = new DLabel;
    diskLabel->setPixmap(iconDisk.pixmap(85, 85));

    DPalette palette1;
    QColor color1("#000000");
    color1.setAlphaF(0.5);
    palette1.setColor(DPalette::WindowText, color1);

    DPalette palette2;
    QColor color2("#000000");
    color2.setAlphaF(0.85);
    palette2.setColor(DPalette::WindowText, color2);

    // 状态提示字体颜色
    DPalette palette4;
    palette4.setColor(DPalette::WindowText, QColor("#526A7F"));

    // 表格内容颜色
    DPalette palette5;
    palette5.setColor(DPalette::Text, QColor("#001A2E"));

    // 表头字体颜色
    DPalette palette6;
    palette6.setColor(DPalette::Text, QColor("#414D68"));

    // 硬盘信息
    HardDiskInfo hardDiskInfo = DMDbusHandler::instance()->getHardDiskInfo(m_devicePath);

    DLabel *serialNumberNameLabel = new DLabel(tr("Serial number")); // 序列号
    DFontSizeManager::instance()->bind(serialNumberNameLabel, DFontSizeManager::T8, QFont::Medium);
    serialNumberNameLabel->setPalette(palette1);

    m_serialNumberValue = new DLabel;
    m_serialNumberValue->setText(hardDiskInfo.m_serialNumber);
    DFontSizeManager::instance()->bind(m_serialNumberValue, DFontSizeManager::T6, QFont::Medium);
    m_serialNumberValue->setPalette(palette2);

    DLabel *userCapacityNameLabel = new DLabel(tr("Storage")); // 用户容量
    DFontSizeManager::instance()->bind(userCapacityNameLabel, DFontSizeManager::T8, QFont::Medium);
    userCapacityNameLabel->setPalette(palette1);

    m_userCapacityValue = new DLabel;
    m_userCapacityValue->setText(hardDiskInfo.m_userCapacity);
    DFontSizeManager::instance()->bind(m_userCapacityValue, DFontSizeManager::T10, QFont::Normal);
    m_userCapacityValue->setPalette(palette2);

    QVBoxLayout *diskInfoLayout = new QVBoxLayout;
    diskInfoLayout->addSpacing(7);
    diskInfoLayout->addWidget(serialNumberNameLabel);
    diskInfoLayout->addWidget(m_serialNumberValue);
    diskInfoLayout->addWidget(userCapacityNameLabel);
    diskInfoLayout->addWidget(m_userCapacityValue);
    diskInfoLayout->addSpacing(5);
    diskInfoLayout->setContentsMargins(0, 0, 0, 0);

    // 硬盘健康状态
    QString healthStateValue = DMDbusHandler::instance()->getDeviceHardStatus(m_devicePath);

    DLabel *healthStateLabel = new DLabel(tr("Health Status")); // 健康状态
    DFontSizeManager::instance()->bind(healthStateLabel, DFontSizeManager::T6, QFont::Medium);

    QIcon iconHealth = Common::getIcon("good");
    DLabel *iconHealthLabel = new DLabel;
    m_healthStateValue = new DLabel;
    DFontSizeManager::instance()->bind(m_healthStateValue, DFontSizeManager::T2, QFont::Medium);

    // 状态颜色
    DPalette paletteStateColor;

    if (0 == healthStateValue.compare("PASSED", Qt::CaseInsensitive)) {
        iconHealth = Common::getIcon("good");
        iconHealthLabel->setPixmap(iconHealth.pixmap(30, 30));
        m_healthStateValue->setText(tr("Good")); // 良好    【警告】Warning
        paletteStateColor.setColor(DPalette::Text, QColor("#00c800"));
        m_healthStateValue->setPalette(paletteStateColor);
    } else if (0 == healthStateValue.compare("Failure", Qt::CaseInsensitive)) {
        iconHealth = Common::getIcon("damage");
        iconHealthLabel->setPixmap(iconHealth.pixmap(30, 30));
        m_healthStateValue->setText(tr("Damaged")); // 损坏
        paletteStateColor.setColor(DPalette::Text, QColor("#E02020"));
        m_healthStateValue->setPalette(paletteStateColor);
    } else {
        iconHealth = Common::getIcon("unknown");
        iconHealthLabel->setPixmap(iconHealth.pixmap(30, 30));
        m_healthStateValue->setText(tr("Unknown")); // 未知
        paletteStateColor.setColor(DPalette::Text, QColor("#777777"));
        m_healthStateValue->setPalette(paletteStateColor);
    }

    QHBoxLayout *healthValueLayout = new QHBoxLayout;
    healthValueLayout->addWidget(iconHealthLabel);
    healthValueLayout->addWidget(m_healthStateValue);
    healthValueLayout->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout *healthStateLayout = new QVBoxLayout;
    healthStateLayout->addSpacing(5);
    healthStateLayout->addWidget(healthStateLabel, 0, Qt::AlignCenter);
    healthStateLayout->addSpacing(10);
    healthStateLayout->addLayout(healthValueLayout);
    healthStateLayout->addSpacing(7);
    healthStateLayout->setContentsMargins(0, 0, 0, 0);

    // 温度
    DLabel *temperatureLabel = new DLabel(tr("Temperature")); // 温度
    DFontSizeManager::instance()->bind(temperatureLabel, DFontSizeManager::T6, QFont::Medium);
    temperatureLabel->setPalette(palette2);

    m_temperatureValue = new DLabel("-°C");
    DFontSizeManager::instance()->bind(m_temperatureValue, DFontSizeManager::T2, QFont::Medium);
    m_temperatureValue->setPalette(palette2);

    QVBoxLayout *temperatureLayout = new QVBoxLayout;
    temperatureLayout->addSpacing(5);
    temperatureLayout->addWidget(temperatureLabel, 0, Qt::AlignRight);
    temperatureLayout->addSpacing(10);
    temperatureLayout->addWidget(m_temperatureValue);
    temperatureLayout->addSpacing(7);
    temperatureLayout->setContentsMargins(0, 0, 10, 0);

    DFrame *infoWidget = new DFrame;
    infoWidget->setBackgroundRole(DPalette::ItemBackground);
    infoWidget->setMinimumSize(706, 108);

    QHBoxLayout *topLayout = new QHBoxLayout(infoWidget);
    topLayout->addWidget(diskLabel);
    topLayout->addLayout(diskInfoLayout);
    topLayout->addStretch();
    topLayout->addLayout(healthStateLayout);
    topLayout->addSpacing(50);
    topLayout->addLayout(temperatureLayout);
    topLayout->setContentsMargins(10, 10, 10, 10);

    // 属性列表
    m_tableView = new DTableView(this);
    m_standardItemModel = new QStandardItemModel(this);

    m_tableView->setShowGrid(false);
    m_tableView->setFrameShape(QAbstractItemView::NoFrame);
    m_tableView->verticalHeader()->setVisible(false); //隐藏列表头
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::NoSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setPalette(palette5);

    m_diskHealthHeaderView = new DiskHealthHeaderView(Qt::Horizontal, this);
    m_tableView->setHorizontalHeader(m_diskHealthHeaderView);

//    m_tableView->setFont(QFont("SourceHanSansSC", 10, 50));
    QFont fontHeader = DFontSizeManager::instance()->get(DFontSizeManager::T6, QFont::Medium);
    m_tableView->horizontalHeader()->setFont(fontHeader);
    m_tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignCenter);
    m_tableView->horizontalHeader()->setPalette(palette6);
//    m_tableView->horizontalHeader()->setFixedHeight(30);


    m_diskHealthDetectionDelegate = new DiskHealthDetectionDelegate(this);
    m_tableView->setItemDelegate(m_diskHealthDetectionDelegate);
    if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType) {
        m_diskHealthDetectionDelegate->setTextColor(QColor("#C0C6D4"));
    } else if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType) {
        m_diskHealthDetectionDelegate->setTextColor(QColor("#001A2E"));
    }

    m_standardItemModel->setColumnCount(7);
    m_standardItemModel->setHeaderData(0, Qt::Horizontal, tr("ID"));
    m_standardItemModel->setHeaderData(1, Qt::Horizontal, tr("Status")); // 状态
    m_standardItemModel->setHeaderData(2, Qt::Horizontal, tr("Current")); // 当前值
    m_standardItemModel->setHeaderData(3, Qt::Horizontal, tr("Worst")); // 历史最差值
    m_standardItemModel->setHeaderData(4, Qt::Horizontal, tr("Threshold")); // 临界值
    m_standardItemModel->setHeaderData(5, Qt::Horizontal, tr("Raw Value")); // 原始数据
    m_standardItemModel->setHeaderData(6, Qt::Horizontal, tr("Attribute name")); // 属性名称

    m_tableView->setModel(m_standardItemModel);
    m_tableView->horizontalHeader()->setStretchLastSection(true);// 设置最后一列自适应
//    m_tableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
//    m_tableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents); // 设置第三列自适应列宽
//    m_tableView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents); // 设置第四列自适应列宽

    m_tableView->setColumnWidth(0, 70);
    m_tableView->setColumnWidth(1, 70);
    m_tableView->setColumnWidth(2, 70);
    m_tableView->setColumnWidth(3, 100);
    m_tableView->setColumnWidth(4, 70);
    m_tableView->setColumnWidth(5, 130);
//    m_tableView->setColumnWidth(6, 186);

    for (int i = 0; i < m_hardDiskStatusInfoList.count(); i++) {
        HardDiskStatusInfo hardDiskStatusInfo = m_hardDiskStatusInfoList.at(i);

        if (hardDiskStatusInfo.m_id == "194" || hardDiskStatusInfo.m_attributeName == "Temperature") {
            QString value;
            for (int i = 0; i < hardDiskStatusInfo.m_rawValue.size(); i++) {
                if (hardDiskStatusInfo.m_rawValue.at(i) >= "0" && hardDiskStatusInfo.m_rawValue.at(i) <= "9") {
                    value += hardDiskStatusInfo.m_rawValue.at(i);
                } else {
                    break;
                }
            }

            if (!value.isEmpty()) {
                m_temperatureValue->setText(QString("%1°C").arg(value));
            }
        }

        QList<QStandardItem*> itemList;

        if (!hardDiskStatusInfo.m_id.isEmpty()) {
            itemList << new QStandardItem(hardDiskStatusInfo.m_id);
        } else {
            itemList << new QStandardItem("-");
        }

        if (!hardDiskStatusInfo.m_whenFailed.isEmpty()) {
            if (hardDiskStatusInfo.m_whenFailed == "-") {
                itemList << new QStandardItem("G");
            } else if(0 == hardDiskStatusInfo.m_whenFailed.compare("In_the_past", Qt::CaseInsensitive)) {
                itemList << new QStandardItem("W");
            } else if(0 == hardDiskStatusInfo.m_whenFailed.compare("FAILING_NOW", Qt::CaseInsensitive)) {
                itemList << new QStandardItem("D");
            } else {
                itemList << new QStandardItem("U");
            }
        } else {
            itemList << new QStandardItem("-");
        }

        if (!hardDiskStatusInfo.m_value.isEmpty()) {
            itemList << new QStandardItem(hardDiskStatusInfo.m_value);
        } else {
            itemList << new QStandardItem("-");
        }

        if (!hardDiskStatusInfo.m_worst.isEmpty()) {
            itemList << new QStandardItem(hardDiskStatusInfo.m_worst);
        } else {
            itemList << new QStandardItem("-");
        }

        if (!hardDiskStatusInfo.m_thresh.isEmpty()) {
            itemList << new QStandardItem(hardDiskStatusInfo.m_thresh);
        } else {
            itemList << new QStandardItem("-");
        }

        if (!hardDiskStatusInfo.m_rawValue.isEmpty()) {
            itemList << new QStandardItem(hardDiskStatusInfo.m_rawValue);
        } else {
            itemList << new QStandardItem("-");
        }

        if (!hardDiskStatusInfo.m_attributeName.isEmpty()) {
            itemList << new QStandardItem(hardDiskStatusInfo.m_attributeName);
        } else {
            itemList << new QStandardItem("-");
        }

        m_standardItemModel->appendRow(itemList);
    }

    DFrame *tableWidget = new DFrame;
    tableWidget->setMinimumSize(706, 451);
    QHBoxLayout *tableLayout = new QHBoxLayout(tableWidget);
    tableLayout->addWidget(m_tableView);
    tableLayout->setSpacing(0);
    tableLayout->setContentsMargins(5, 0, 5, 0);

    DLabel *stateTipsLabel = new DLabel;
    stateTipsLabel->setText(tr("Status: (G: Good | W: Warning | D: Damaged | U: Unknown)")); // 状态:(G: 良好 | W: 警告 | D: 损坏 | U: 未知)
    DFontSizeManager::instance()->bind(stateTipsLabel, DFontSizeManager::T8, QFont::Normal);
    stateTipsLabel->setPalette(palette4);

    m_linkButton = new DCommandLinkButton(tr("Export")); // 导出
    QFontMetrics fmCapacity = m_linkButton->fontMetrics();
    int wdith = fmCapacity.width(QString(tr("Export")));
    m_linkButton->setFixedWidth(wdith);

    QWidget *bottomWidget = new QWidget;
    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomWidget);
    bottomLayout->addWidget(stateTipsLabel);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_linkButton);
    bottomLayout->setContentsMargins(0, 0, 0, 0);

    addSpacing(10);
    addContent(infoWidget);
    addSpacing(10);
    addContent(tableWidget);
    addSpacing(10);
    addContent(bottomWidget);
}

void DiskHealthDetectionDialog::initConnections()
{
    connect(m_linkButton, &DCommandLinkButton::clicked, this, &DiskHealthDetectionDialog::onExportButtonClicked);
}

void DiskHealthDetectionDialog::onExportButtonClicked()
{
    //文件保存路径
    QString fileDirPath = QFileDialog::getSaveFileName(this, tr("Save File"), "CheckHealthInfo.txt", tr("Text files (*.txt)"));// 文件保存   硬盘健康检测信息   文件类型
    if (fileDirPath.isEmpty()) {
        return;
    }

    QString strFileName = "/" + fileDirPath.split("/").last();
    QString fileDir = fileDirPath.split(strFileName).first();

    QFileInfo fileInfo;
    fileInfo.setFile(fileDir);
    QDir dir(fileDir);

    if (!dir.exists()) {
        DFloatingMessage *floMsg = new DFloatingMessage(DFloatingMessage::ResidentType);
        floMsg->setIcon(QIcon::fromTheme("://icons/deepin/builtin/warning.svg"));
        floMsg->setMessage(tr("Wrong path")); // 路径错误
        DMessageManager::instance()->sendMessage(this, floMsg);
        DMessageManager::instance()->setContentMargens(this, QMargins(0, 0, 0, 20));

        return;
    }

    if (!fileInfo.isWritable()) {
        DFloatingMessage *floMsg = new DFloatingMessage(DFloatingMessage::ResidentType);
        floMsg->setIcon(QIcon::fromTheme("://icons/deepin/builtin/warning.svg"));
        floMsg->setMessage(tr("You do not have permission to access this path")); // 您无权访问该路径
        DMessageManager::instance()->sendMessage(this, floMsg);
        DMessageManager::instance()->setContentMargens(this, QMargins(0, 0, 0, 20));
    } else {
        if (!fileDirPath.contains(".txt")) {
            fileDirPath = fileDirPath + ".txt";
        }

        QFile file(fileDirPath);
        if (file.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
            QTextStream out(&file);

            QString headers = tr("ID") + "," + tr("Status") + "," + tr("Current") + "," + tr("Worst")
                    + "," + tr("Threshold") + "," + tr("Raw Value") + "," + tr("Attribute name") + "\n";
            out << headers;

            for (int i = 0; i < m_standardItemModel->rowCount(); i++) {
                QString strInfo = m_standardItemModel->item(i, 0)->text() + ","
                        + m_standardItemModel->item(i, 1)->text() + ","
                        + m_standardItemModel->item(i, 2)->text() + ","
                        + m_standardItemModel->item(i, 3)->text() + ","
                        + m_standardItemModel->item(i, 4)->text() + ","
                        + m_standardItemModel->item(i, 5)->text() + ","
                        + m_standardItemModel->item(i, 6)->text() + "\n";

                out << strInfo;
                out.flush();
            }

            file.close();

            DMessageManager::instance()->sendMessage(this, QIcon::fromTheme("://icons/deepin/builtin/ok.svg"), tr("Export successful")); // 导出成功
            DMessageManager::instance()->setContentMargens(this, QMargins(0, 0, 0, 20));
        } else {
            DMessageManager::instance()->sendMessage(this, QIcon::fromTheme("://icons/deepin/builtin/warning.svg"), tr("Export failed")); // 导出失败
            DMessageManager::instance()->setContentMargens(this, QMargins(0, 0, 0, 20));
        }
    }
}


