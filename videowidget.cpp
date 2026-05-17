#include "videowidget.h"
#include <QPainter>

VideoOpenGLWidget::VideoOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

VideoOpenGLWidget::~VideoOpenGLWidget()
{
    // 销毁 OpenGL 资源前，必须获得当前上下文！
    makeCurrent();

    // // 假设你有一个纹理指针
    // if (m_texture) {
    //     m_texture->destroy(); // 销毁显存中的纹理
    //     delete m_texture;
    //     m_texture = nullptr;
    // }

    // 如果你有其他的 VBO, VAO, PBO 也要在这里释放

    doneCurrent(); // 释放上下文
}

void VideoOpenGLWidget::initializeGL()
{
    isInitialized = true;
}

void VideoOpenGLWidget::setFrame(const QImage &img)
{
    if (!isInitialized) return;
    QMutexLocker locker(&mutex);
    currentFrame = img.copy();
    update(); // 触发 repaint
}

void VideoOpenGLWidget::paintGL()
{
    QPainter painter(this);

    painter.fillRect(rect(), Qt::black);

    mutex.lock();
    if (!currentFrame.isNull()) {
        QImage scaled = currentFrame.scaled(size(), Qt::KeepAspectRatio);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawImage(QPoint(x, y), scaled);
    }
    mutex.unlock();
}


