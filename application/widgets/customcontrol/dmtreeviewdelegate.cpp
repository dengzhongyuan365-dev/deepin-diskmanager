/**
 * @copyright 2020-2020 Uniontech Technology Co., Ltd.
 *
 * @file dmtreeviewdelegate.cpp
 *
 * @brief 设备树结构代理类
 *
 * @date 2020-09-18 09:06
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
#include "dmtreeviewdelegate.h"
#include "common.h"
#include "dmtreeview.h"

#include <DFontSizeManager>

#include <QDebug>
#include <QTextOption>

DmTreeviewDelegate::DmTreeviewDelegate(QAbstractItemView *parent)
    : DStyledItemDelegate(parent)
    , m_parentView(parent)
{
    m_parentPb = DApplicationHelper::instance()->palette(m_parentView);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this,
            &DmTreeviewDelegate::onHandleChangeTheme);
}

QSize DmTreeviewDelegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    Q_UNUSED(option);

    DiskInfoData infoData = index.data(Qt::UserRole + 1).value<DiskInfoData>();
    if (infoData.m_level == 0) {
        return QSize(180, 62);
    }

    return QSize(180, 55);
}

void DmTreeviewDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if(!index.isValid()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QVariant varData = index.data(Qt::UserRole + 1);
    DiskInfoData data = varData.value<DiskInfoData>();
    QRect rect;
    if (data.m_level == 0) {
        rect.setX(option.rect.x());
    } else {
        rect.setX(option.rect.x() + 10);
    }

    if (data.m_level == 0) {
        rect.setY(option.rect.y() + 10);
    } else {
        rect.setY(option.rect.y());
    }

    rect.setWidth(option.rect.width());
//    rect.setHeight(option.rect.height()); // 分区节点间有间隔
    // 去掉分区节点间隔
    if (data.m_level == 0) {
        rect.setHeight(option.rect.height());
    } else {
        rect.setHeight(option.rect.height() + 1);
    }

    painter->setRenderHints(QPainter::SmoothPixmapTransform);
    QRect paintRect = QRect(rect.left(), rect.top(), rect.width() - 19, rect.height());
    QPainterPath path;
    const int radius = 8;
    path.moveTo(paintRect.bottomRight() - QPoint(0, radius));
    path.lineTo(paintRect.topRight() + QPoint(0, radius));
    path.arcTo(QRect(QPoint(paintRect.topRight() - QPoint(radius * 2, 0)),
                     QSize(radius * 2, radius * 2)), 0, 90);
    path.lineTo(paintRect.topLeft() + QPoint(radius, 0));
    path.arcTo(QRect(QPoint(paintRect.topLeft()), QSize(radius * 2, radius * 2)), 90, 90);
    path.lineTo(paintRect.bottomLeft() - QPoint(0, radius));
    path.arcTo(QRect(QPoint(paintRect.bottomLeft() - QPoint(0, radius * 2)),
                     QSize(radius * 2, radius * 2)), 180, 90);
    path.lineTo(paintRect.bottomLeft() + QPoint(radius, 0));
    path.arcTo(QRect(QPoint(paintRect.bottomRight() - QPoint(radius * 2, radius * 2)),
                     QSize(radius * 2, radius * 2)), 270, 90);
//    设置分区背景色
//            if (data.level == 1) {
//        QBrush brush = m_parentPb.itemBackground();
//        painter->setBrush(brush);
//        painter->fillPath(path, brush);
//    }

    // 设置分区选中时文本颜色
    if ((option.state & QStyle::State_Selected) && (data.m_level == 1)) {
        QColor fillColor = m_parentPb.color(DPalette::Normal, DPalette::Highlight);
        painter->setBrush(QBrush(fillColor));
        painter->fillPath(path, painter->brush());
        painter->setPen(QPen(Qt::white));
    }
    DmTreeview *treeView = qobject_cast<DmTreeview *>(m_parentView);
//    if (treeView == nullptr) {
//    }

    QIcon directionIcon;
    int pixmapWidth = 8; // 伸缩按钮宽
    int pixmapHeight = 8; // 伸缩按钮高
    if (treeView->getItemByIndex(index) && treeView->getItemByIndex(index)->hasChildren()) {
        if (treeView->isExpanded(index)) {
            directionIcon = Common::getIcon("arrow");
            pixmapWidth = 10;
            pixmapHeight = 7;
        } else {
            directionIcon = Common::getIcon("arrow_right");
            pixmapWidth = 8;
            pixmapHeight = 11;
        }
    }

    QRect lefticon1Rect;
    QRect lefticonRect2;
    QRect textRect;
    QRect textRect1;
    QString text = data.m_diskPath;
    QString text1 = data.m_diskSize;
    QString text2 = data.m_partitonPath;
    QString text3 = data.m_partitionSize;
    if (data.m_level == 0) {
        lefticon1Rect.setRect(paintRect.left() + 8, paintRect.top() + 20, pixmapWidth, pixmapHeight);
        painter->drawPixmap(lefticon1Rect, directionIcon.pixmap(17, 17));
        lefticonRect2.setRect(paintRect.left() + 20, paintRect.top() + 4, 40, 40);
        QIcon icon = Common::getIcon("treedisk");
        painter->drawPixmap(lefticonRect2, icon.pixmap(38, 38));
        QTextOption option;
        QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);
        QColor textcolor = m_parentPb.color(DPalette::Normal, DPalette::Text);
        painter->setPen(textcolor);
        painter->setFont(font);

        // 获取文本宽度
        QFontMetrics fmCapacity = painter->fontMetrics();
        int textWidth = fmCapacity.width(text);

        textRect.setRect(paintRect.left() + 60, paintRect.top() + 5, textWidth + 1, 100);
        painter->drawText(textRect, text);
        QColor text1color = m_parentPb.color(DPalette::Normal, DPalette::TextTips);
        painter->setPen(text1color);
        font = DFontSizeManager::instance()->get(DFontSizeManager::T8);
        painter->setFont(font);
        textRect1.setRect(paintRect.left() + 60, paintRect.top() + 25, 100, 100);
        painter->drawText(textRect1, text1);
    } else {
        lefticon1Rect.setRect(paintRect.left() + 25, paintRect.top() + 10, 30, 30);
        QIcon icon = Common::getIcon("harddisk");
        QIcon icon1 = Common::getIcon("mounticon");
        QIcon icon2 = Common::getIcon("uninstallicon");
        QIcon icon3 = Common::getIcon("hidden");
        painter->drawPixmap(lefticon1Rect, icon.pixmap(28, 28));
        QRect mounticonRect = QRect(paintRect.left() + 45, paintRect.top() + 25, 10, 10);

//        // 获取分区是否隐藏
//        int hide = 0;
//        if (data.mountpoints.isEmpty() || data.mountpoints == "/recovery") {
//            int result = DMDbusHandler::instance()->getPartitionHiddenFlag(data.diskpath, data.partitonpath);
//            if (1 == result) {
//                hide = 1;
//            }
//        }

        if (data.m_fstype == "unallocated") {
            painter->drawPixmap(mounticonRect, icon2.pixmap(10, 10));
        } else if (1 == data.m_flag) {
            painter->drawPixmap(mounticonRect, icon3.pixmap(10, 10));
        } else {
            painter->drawPixmap(mounticonRect, icon1.pixmap(10, 10));
        }

        QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);
        if ((option.state & QStyle::State_Selected) && (data.m_level == 1)) {
            QColor textColor = m_parentPb.color(DPalette::Normal, DPalette::HighlightedText);
            painter->setPen(textColor);
            painter->setFont(font);
            textRect.setRect(paintRect.left() + 65, paintRect.top() + 8, 106, 100);
            QString text = painter->fontMetrics().elidedText(text2, Qt::ElideMiddle, textRect.width());
            painter->drawText(textRect, text);
            QColor text1Color = m_parentPb.color(DPalette::Normal, DPalette::HighlightedText);
            painter->setPen(text1Color);
            font = DFontSizeManager::instance()->get(DFontSizeManager::T8);
            painter->setFont(font);
            textRect1.setRect(paintRect.left() + 65, paintRect.top() + 28, 100, 100);
            painter->drawText(textRect1, text3);
        } else {
            QColor textColor = m_parentPb.color(DPalette::Normal, DPalette::Text);
            painter->setPen(textColor);
            painter->setFont(font);
            textRect.setRect(paintRect.left() + 65, paintRect.top() + 8, 106, 100);
            QString text = painter->fontMetrics().elidedText(text2, Qt::ElideMiddle, textRect.width());
            painter->drawText(textRect, text);
            QColor text1Color = m_parentPb.color(DPalette::Normal, DPalette::TextTips);
            painter->setPen(text1Color);
            font = DFontSizeManager::instance()->get(DFontSizeManager::T8);
            painter->setFont(font);
            textRect1.setRect(paintRect.left() + 65, paintRect.top() + 28, 100, 100);
            painter->drawText(textRect1, text3);
        }
    }

    painter->restore();
}
void DmTreeviewDelegate::onHandleChangeTheme()
{
    m_parentPb = Dtk::Gui::DGuiApplicationHelper::instance()->applicationPalette();
    m_parentView->update(m_parentView->currentIndex());
}
