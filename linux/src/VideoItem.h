#pragma once
#include "PlayerBridge.h"
#include <QImage>
#include <QQuickItem>
class PlayerBridge;

// Letterboxed video surface. v1 uploads the enhanced RGBA frame as a Qt Quick texture.
class VideoItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* bridge READ player WRITE setPlayer NOTIFY playerChanged)
public:
    explicit VideoItem(QQuickItem* parent = nullptr);
    QObject* player() const { return player_; }
    void setPlayer(QObject* p);
signals:
    void playerChanged();
protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
private:
    PlayerBridge* player_ = nullptr;
    QImage image_;
    bool dirty_ = false;
    enum class Kind { None, Image, Live } kind_ = Kind::None;
    quint64 nodeGen_ = 0;
};
