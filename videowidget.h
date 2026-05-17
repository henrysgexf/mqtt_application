#pragma once
#include <QOpenGLWidget>
#include <QImage>
#include <QMutex>

class VideoOpenGLWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit VideoOpenGLWidget(QWidget *parent = nullptr);

    ~VideoOpenGLWidget();

    void setFrame(const QImage &img);

    bool isInitialized = false;

    void initializeGL();

protected:
    void paintGL() override;

private:
    QImage currentFrame;
    QMutex mutex;
};
