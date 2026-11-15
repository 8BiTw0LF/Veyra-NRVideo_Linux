#include "VideoItem.h"
#include "PlayerBridge.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include "LiveVulkan.h"
#include <QtQuick/qsgtexture_platform.h>
#include <vulkan/vulkan.h>

VideoItem::VideoItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    // HDR output: the video is drawn through a layer, which is only redrawn when this item is marked dirty. The live
    // path writes new pictures into the same Vulkan image without touching the item, so mark it every frame.
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* w) {
        if (w) connect(w, &QQuickWindow::afterAnimating, this, [this] {
            if (player_ && player_->hdrOutputActive() && player_->liveVulkanActive()) update();
        });
    });
}

void VideoItem::setPlayer(QObject* p) {
    auto* b = qobject_cast<PlayerBridge*>(p);
    if (b == player_) return;
    if (player_) disconnect(player_, nullptr, this, nullptr);
    player_ = b;
    if (player_) connect(player_, &PlayerBridge::frameReady, this, [this] {
        image_ = player_->currentImage();
        dirty_ = true;
        update();
    });
    emit playerChanged();
}

QSGNode* VideoItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    // ---- zero-copy live capture: wrap the Vulkan image the chain just rendered, no CPU involved ----
    if (player_ && player_->liveVulkanActive()) {
        LiveVulkan* lv = player_->liveVulkan();
        const VkImage img = reinterpret_cast<VkImage>(uintptr_t(lv->imageHandle()));
        const QSize sz = lv->imageSize();
        if (!img || sz.isEmpty()) { delete node; kind_ = Kind::None; return nullptr; }
        if (node && kind_ != Kind::Live) { delete node; node = nullptr; }
        if (!node) {
            node = new QSGSimpleTextureNode;
            node->setOwnsTexture(true);
            node->setFiltering(QSGTexture::Linear);
            nodeGen_ = 0;
        }
        kind_ = Kind::Live;
        if (lv->generation() != nodeGen_ || !node->texture()) {
            node->setTexture(QNativeInterface::QSGVulkanTexture::fromNative(img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, window(), sz,
                                                                            QQuickWindow::TextureIsOpaque));
            nodeGen_ = lv->generation();
        }
        const double a = player_->videoAspect() > 0 ? player_->videoAspect() : double(sz.width()) / sz.height();
        double w = width(), h = width() / a;
        if (h > height()) { h = height(); w = h * a; }
        node->setRect(QRectF((width() - w) / 2, (height() - h) / 2, w, h));
        // the texture's content changed (new picture in the same image): lets a layer above re-render (HDR output)
        if (player_->hdrOutputActive()) node->markDirty(QSGNode::DirtyMaterial);
        return node;
    }
    if (node && kind_ == Kind::Live) { delete node; node = nullptr; }
    if (image_.isNull()) { delete node; kind_ = Kind::None; return nullptr; }
    kind_ = Kind::Image;
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    if (dirty_ || !node->texture()) {
        node->setTexture(window()->createTextureFromImage(image_, QQuickWindow::TextureIsOpaque));
        dirty_ = false;
    }
    // Fit while preserving the display aspect (SAR-corrected).
    const double a = player_ ? player_->videoAspect() : double(image_.width()) / image_.height();
    double w = width(), h = width() / a;
    if (h > height()) { h = height(); w = h * a; }
    node->setRect(QRectF((width() - w) / 2, (height() - h) / 2, w, h));
    return node;
}
