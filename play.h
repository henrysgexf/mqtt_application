#ifndef PLAY_H
#define PLAY_H

#include <QWidget>
#include <QMqttClient>
#include <QUdpSocket>
#include <QVector>
#include <QPointF>
#include <QImage>
#include <QTimer>
#include "videowidget.h"
#include "robomaster_protocol.pb.h"
#include <QMqttMessage>
#include <QLabel>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <QMap>
#include <QPointer>
#include <QMqttSubscription>

#include "hevcnalparser.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Play; }
QT_END_NAMESPACE

Q_DECLARE_METATYPE(robomaster::GameStatus)
Q_DECLARE_METATYPE(robomaster::GlobalUnitStatus)

struct FrameBuffer
{
    int totalSize = 0;
    int receivedSize = 0;
    int expectedSlices = 0;
    QMap<int,QByteArray> slices;
    qint64 timestamp = 0;
};

class Play : public QWidget
{
    Q_OBJECT

public slots:
    void onGameStatusReceived(const QMqttMessage &message);
    void onGlobalUnitStatusReceived(const QMqttMessage &message);

public:
    explicit Play(QMqttClient *client, QWidget *parent = nullptr);
    ~Play();

    void setupMqtt();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;   // 新增：处理窗口拖动

private slots:
    void processVideoData();
    void sendKeyboardMouseControl();
    void onMqttMessageReceived(const QMqttMessage &msg);
    void updateAirSupportStatus(const QString &status);
    void onAirSupportStatusSyncReceived(const QByteArray &payload);
    void onBuffStatusReceived(const QByteArray &payload);
    void onRobotRealtimeReceived(const QByteArray &payload);

private:
    Ui::Play *ui;
    QMqttClient *mqttClient;
    QUdpSocket *radarSocket = nullptr;  // 显式初始化，避免野指针
    QUdpSocket *videoSocket;

    QByteArray videoBuffer;
    QByteArray streamBuffer;
    QTimer *reconnectTimer;
    QVector<QPointF> radarPoints;
    QImage videoFrame;
    QByteArray vps, sps, pps;
    bool gotIDR = false;

    QFile mouseLogFile;
    QTextStream mouseLogStream;

    SwsContext* swsCtx = nullptr;
    uint8_t* rgbBuffer = nullptr;
    int rgbBufferSize = 0;
    int videoWidth = 0;
    int videoHeight = 0;

    // 解码线程相关
    QThread *decodeThread = nullptr;
    QMutex decodeMutex;
    QQueue<QByteArray> decodeQueue;
    QWaitCondition decodeCond;
    bool decodeRunning;
    QTimer *cleanupTimer;

    void decodeLoop();
    void renderImage(const QImage &img);

    HevcNalParser nalParser;

    AVCodecContext *codecCtx;
    AVFrame *frame;

    QMap<uint16_t, FrameBuffer> frameBuffers;

    // 键鼠状态
    uint32_t keyboardMask;
    bool leftButtonDown;
    bool rightButtonDown;
    bool midButtonDown;

    // 鼠标速度控制（重构后）
    float targetSpeedX = 0.0f;       // 由鼠标位移计算的目标速度
    float targetSpeedY = 0.0f;
    float currentSpeedX = 0.0f;      // 经平滑/衰减后的实际发送速度
    float currentSpeedY = 0.0f;
    QElapsedTimer lastMouseMoveTimer;
    static constexpr int stopTimeoutMs = 50;        // 无鼠标移动后多少ms开始衰减
    static constexpr float smoothFactor = 0.4f;     // 向目标速度逼近的系数
    static constexpr float decayFactor = 0.85f;     // 无移动时的衰减系数
    static constexpr float mouseSensitivity = 0.08f; // 鼠标灵敏度（像素到速度），需实测调整

    QPointF lastMousePos;
    QTimer *sendTimer;

    bool mouseLocked;
    QPointF lockCenter;

    void lockMouse();
    void unlockMouse();
    void updateMouseLock();

    int keyToBit(int key);
    void renderFrame(AVFrame* frame);

    void updateGameStatusDisplay(const robomaster::GameStatus &status);
    void updateGlobalUnitDisplay(const robomaster::GlobalUnitStatus &status);
    QPointer<QMqttSubscription> subGameStatus;
    QPointer<QMqttSubscription> subGlobalStatus;
    QMqttSubscription *subAll = nullptr;

    bool airSupportRequested = false;
    void sendAirSupportCommand();

signals:
    void gameStatusReceived(const robomaster::GameStatus &status);
    void globalUnitStatusReceived(const robomaster::GlobalUnitStatus &status);
    void airSupportCmdReceived(QByteArray);
    void airSupportFeedbackReceived(QByteArray);
    void radarRobotPosReceived(QByteArray);
    void buffStatusReceived(QByteArray);
    void penaltyStatusReceived(QByteArray);
    void robotPoseReceived(QByteArray);
    void robotModuleStatusReceived(QByteArray);
    void robotRealtimeReceived(QByteArray);
    void robotConfigReceived(QByteArray);
    void globalEventReceived(QByteArray);
    void globalMechanismReceived(QByteArray);
    void logisticsReceived(QByteArray);
};

#endif
