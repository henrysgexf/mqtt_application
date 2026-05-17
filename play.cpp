#include "play.h"
#include "ui_play.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QDebug>
#include <QCursor>
#include <QLabel>
#include <QElapsedTimer>
#include <QMoveEvent>

using namespace robomaster;

Play::Play(QMqttClient *client, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Play)
    , mqttClient(client)
    , keyboardMask(0)
    , leftButtonDown(false)
    , rightButtonDown(false)
    , midButtonDown(false)
    , mouseLocked(false)
    , targetSpeedX(0.0f)
    , targetSpeedY(0.0f)
    , currentSpeedX(0.0f)
    , currentSpeedY(0.0f)
{
    ui->setupUi(this);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    lastMousePos = QCursor::pos();
    lastMouseMoveTimer.start();

    sendTimer = new QTimer(this);
    connect(sendTimer, &QTimer::timeout, this, &Play::sendKeyboardMouseControl);
    sendTimer->start(15);

    videoSocket = new QUdpSocket(this);
    videoSocket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 2 * 1024 * 1024);

    if (!videoSocket->bind(QHostAddress::AnyIPv4, 3334)) {
        qDebug() << "绑定 UDP 端口 3334 失败:" << videoSocket->errorString();
    } else {
        qDebug() << "UDP 端口 3334 绑定成功，接收缓冲区大小:" << videoSocket->socketOption(QAbstractSocket::ReceiveBufferSizeSocketOption).toInt();
    }
    connect(videoSocket, &QUdpSocket::readyRead, this, &Play::processVideoData);

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    codecCtx = avcodec_alloc_context3(codec);
    codecCtx->thread_count = 4;
    codecCtx->thread_type = FF_THREAD_SLICE;
    codecCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;
    avcodec_open2(codecCtx, codec, nullptr);
    frame = av_frame_alloc();
    swsCtx = nullptr;

    ui->gameStatus->setText("Waiting for connection...");
    qRegisterMetaType<robomaster::GameStatus>("robomaster::GameStatus");
    qRegisterMetaType<robomaster::GlobalUnitStatus>("robomaster::GlobalUnitStatus");

    connect(mqttClient, &QMqttClient::connected, this, &Play::setupMqtt);
    connect(this, &Play::gameStatusReceived, this, &Play::updateGameStatusDisplay);
    connect(this, &Play::globalUnitStatusReceived, this, &Play::updateGlobalUnitDisplay);
    connect(this, &Play::airSupportFeedbackReceived, this, &Play::onAirSupportStatusSyncReceived);
    connect(this, &Play::buffStatusReceived, this, &Play::onBuffStatusReceived);
    connect(this, &Play::robotRealtimeReceived, this, &Play::onRobotRealtimeReceived);

    ui->VideoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->VideoWidget->setFocusPolicy(Qt::NoFocus);

    // 创建定时器，独立清理超时帧
    cleanupTimer = new QTimer(this);
    connect(cleanupTimer, &QTimer::timeout, this, [this]() {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<uint16_t> timeoutKeys;
        for (auto it = frameBuffers.begin(); it != frameBuffers.end(); ++it) {
            if (now - it->timestamp > 100)
                timeoutKeys.append(it.key());
        }
        for (uint16_t key : timeoutKeys)
            frameBuffers.remove(key);
    });
    cleanupTimer->start(100);

    // 启动解码线程
    decodeRunning = true;
    decodeThread = QThread::create([this] { decodeLoop(); });
    decodeThread->start();

    // 打开鼠标日志文件
    mouseLogFile.setFileName("mouse_log.txt");
    if (mouseLogFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        mouseLogStream.setDevice(&mouseLogFile);
        mouseLogStream << "timestamp,delta_x,delta_y,target_speed_x,target_speed_y,current_speed_x,current_speed_y\n";
    } else {
        qWarning() << "无法打开鼠标日志文件：" << mouseLogFile.errorString();
    }
}

Play::~Play()
{
    if (videoSocket) {
        videoSocket->disconnectFromHost();
        videoSocket->close();
    }
    if (swsCtx) {
        sws_freeContext(swsCtx);
        swsCtx = nullptr;
    }
    if (codecCtx) {
        avcodec_free_context(&codecCtx);
    }
    if (frame) {
        av_frame_free(&frame);
    }
    if (rgbBuffer) {
        free(rgbBuffer);
        rgbBuffer = nullptr;
    }

    decodeRunning = false;
    decodeCond.wakeAll();
    if (decodeThread) {
        decodeThread->wait();   // 直接 wait，无需 quit()（该线程无事件循环）
        delete decodeThread;
    }
    if (cleanupTimer) {
        cleanupTimer->stop();
        delete cleanupTimer;
    }
    if (radarSocket) {
        radarSocket->close();
        delete radarSocket;
    }

    if (mouseLogFile.isOpen()) {
        mouseLogStream.flush();
        mouseLogFile.close();
    }
    delete ui;
}

void Play::setupMqtt()
{
    if (mqttClient->state() != QMqttClient::Connected)
        return;
    qDebug() << "MQTT setup";
    ui->gameStatus->setText("MQTT Connected");

    lockMouse();

    subGameStatus = mqttClient->subscribe(QMqttTopicFilter("GameStatus"));
    subGlobalStatus = mqttClient->subscribe(QMqttTopicFilter("GlobalUnitStatus"));
    if (subGameStatus) {
        connect(subGameStatus, &QMqttSubscription::messageReceived,
                this, &Play::onGameStatusReceived);
    }
    if (subGlobalStatus) {
        connect(subGlobalStatus, &QMqttSubscription::messageReceived,
                this, &Play::onGlobalUnitStatusReceived);
    }

    subAll = mqttClient->subscribe(QMqttTopicFilter("#"));
    if (subAll) {
        connect(subAll, &QMqttSubscription::messageReceived,
                this, &Play::onMqttMessageReceived);
    }
}

void Play::onMqttMessageReceived(const QMqttMessage &msg)
{
    QString topic = msg.topic().name();
    QByteArray payload = msg.payload();

    if (topic == "AirSupportCommand")
        emit airSupportCmdReceived(payload);
    else if (topic == "AirSupportStatusSync")
        emit airSupportFeedbackReceived(payload);
    else if (topic == "RadarInfoToClient")
        emit radarRobotPosReceived(payload);
    else if (topic == "Buff")
        emit buffStatusReceived(payload);
    else if (topic == "PenaltyInfo")
        emit penaltyStatusReceived(payload);
    else if (topic == "RobotPosition")
        emit robotPoseReceived(payload);
    else if (topic == "RobotModuleStatus")
        emit robotModuleStatusReceived(payload);
    else if (topic == "RobotDynamicStatus")
        emit robotRealtimeReceived(payload);
    else if (topic == "RobotStaticStatus")
        emit robotConfigReceived(payload);
    else if (topic == "Event")
        emit globalEventReceived(payload);
    else if (topic == "GlobalSpecialMechanism")
        emit globalMechanismReceived(payload);
    else if (topic == "GlobalLogisticsStatus")
        emit logisticsReceived(payload);
}

void Play::lockMouse()
{
    if (mouseLocked) return;
    mouseLocked = true;
    setFocus();
    activateWindow();
    raise();
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    lockCenter = mapToGlobal(rect().center());
    QCursor::setPos(lockCenter.toPoint());
    setCursor(Qt::BlankCursor);
}

void Play::unlockMouse()
{
    if (!mouseLocked) return;
    mouseLocked = false;
    setCursor(Qt::ArrowCursor);
}

void Play::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (mouseLocked)
            unlockMouse();
        else
            lockMouse();
        return;
    }
    if (event->key() == Qt::Key_F11) {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
        return;
    }
    int bit = keyToBit(event->key());
    if (bit >= 0)
        keyboardMask |= (1 << bit);
    if (event->key() == Qt::Key_H)
        sendAirSupportCommand();
}

void Play::keyReleaseEvent(QKeyEvent *event)
{
    int bit = keyToBit(event->key());
    if (bit >= 0)
        keyboardMask &= ~(1 << bit);
}

void Play::mouseMoveEvent(QMouseEvent *event)
{
    if (mouseLocked) {
        QPointF globalPos = event->globalPosition();
        QPointF delta = globalPos - lockCenter;

        // 比例控制：位移直接映射为速度，钳位 [-100, 100]
        targetSpeedX = qBound(-100.0f, static_cast<float>(delta.x()) * mouseSensitivity, 100.0f);
        targetSpeedY = qBound(-100.0f, static_cast<float>(delta.y()) * mouseSensitivity, 100.0f);

        lastMouseMoveTimer.start();

        // 写入鼠标日志（供后期调参与分析）
        if (mouseLogFile.isOpen()) {
            mouseLogStream << QDateTime::currentMSecsSinceEpoch() << ","
                           << delta.x() << "," << delta.y() << ","
                           << targetSpeedX << "," << targetSpeedY << ","
                           << currentSpeedX << "," << currentSpeedY << "\n";
        }

        // 重置光标到中心
        QCursor::setPos(lockCenter.toPoint());
    } else {
        lastMousePos = event->globalPosition();
    }
}

void Play::sendKeyboardMouseControl()
{
    if (mqttClient->state() != QMqttClient::Connected) return;

    // 速度平滑与衰减逻辑（统一在此处理，避免多通道冲突）
    if (lastMouseMoveTimer.isValid() && lastMouseMoveTimer.elapsed() < stopTimeoutMs) {
        // 鼠标在移动：向目标速度平滑逼近
        currentSpeedX += (targetSpeedX - currentSpeedX) * smoothFactor;
        currentSpeedY += (targetSpeedY - currentSpeedY) * smoothFactor;
    } else {
        // 鼠标已停止：指数衰减到 0
        currentSpeedX *= decayFactor;
        currentSpeedY *= decayFactor;
        if (qAbs(currentSpeedX) < 1.0f) currentSpeedX = 0.0f;
        if (qAbs(currentSpeedY) < 1.0f) currentSpeedY = 0.0f;
    }

    KeyboardMouseControl msg;
    msg.set_mouse_x(static_cast<int>(currentSpeedX));
    msg.set_mouse_y(static_cast<int>(currentSpeedY));
    msg.set_mouse_z(0);
    msg.set_left_button_down(leftButtonDown);
    msg.set_right_button_down(rightButtonDown);
    msg.set_mid_button_down(midButtonDown);
    msg.set_keyboard_value(keyboardMask);

    char buffer[256];
    if (!msg.SerializeToArray(buffer, sizeof(buffer))) return;
    int size = static_cast<int>(msg.ByteSizeLong());
    mqttClient->publish(QMqttTopicName("KeyboardMouseControl"), QByteArray(buffer, size));
}

void Play::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        leftButtonDown = true;
    else if (event->button() == Qt::RightButton)
        rightButtonDown = true;
    else if (event->button() == Qt::MiddleButton)
        midButtonDown = true;
}

void Play::focusOutEvent(QFocusEvent *event)
{
    if (mouseLocked)
        unlockMouse();
    // 重置所有按键掩码和按钮状态
    keyboardMask = 0;
    leftButtonDown = false;
    rightButtonDown = false;
    midButtonDown = false;
    // 重置鼠标速度，防止失焦后云台继续转动
    targetSpeedX = 0.0f;
    targetSpeedY = 0.0f;
    currentSpeedX = 0.0f;
    currentSpeedY = 0.0f;
    // 立即发送一次清零消息
    sendKeyboardMouseControl();
    QWidget::focusOutEvent(event);
}

void Play::resizeEvent(QResizeEvent *event)
{
    if (mouseLocked) {
        lockCenter = mapToGlobal(rect().center());
        QCursor::setPos(lockCenter.toPoint());
    }
    QWidget::resizeEvent(event);
}

void Play::moveEvent(QMoveEvent *event)
{
    if (mouseLocked) {
        lockCenter = mapToGlobal(rect().center());
        QCursor::setPos(lockCenter.toPoint());
    }
    QWidget::moveEvent(event);
}

void Play::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        leftButtonDown = false;
    else if (event->button() == Qt::RightButton)
        rightButtonDown = false;
    else if (event->button() == Qt::MiddleButton)
        midButtonDown = false;
}

int Play::keyToBit(int key)
{
    switch (key) {
    case Qt::Key_W: return 0;
    case Qt::Key_S: return 1;
    case Qt::Key_A: return 2;
    case Qt::Key_D: return 3;
    case Qt::Key_Shift: return 4;
    case Qt::Key_Control: return 5;
    case Qt::Key_Q: return 6;
    case Qt::Key_E: return 7;
    case Qt::Key_R: return 8;
    case Qt::Key_F: return 9;
    case Qt::Key_G: return 10;
    case Qt::Key_Z: return 11;
    case Qt::Key_X: return 12;
    case Qt::Key_C: return 13;
    case Qt::Key_V: return 14;
    case Qt::Key_B: return 15;
    default: return -1;
    }
}

void Play::processVideoData()
{
    while (videoSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(videoSocket->pendingDatagramSize());
        videoSocket->readDatagram(datagram.data(), datagram.size());
        if (datagram.size() < 8) continue;

        uint16_t frameId = (uint8_t(datagram[0]) << 8) | uint8_t(datagram[1]);
        uint16_t sliceId = (uint8_t(datagram[2]) << 8) | uint8_t(datagram[3]);
        uint32_t frameSize = (uint8_t(datagram[4]) << 24) |
                             (uint8_t(datagram[5]) << 16) |
                             (uint8_t(datagram[6]) << 8)  |
                             uint8_t(datagram[7]);
        QByteArray payload = datagram.mid(8);

        FrameBuffer &buffer = frameBuffers[frameId];
        buffer.totalSize = frameSize;
        if (buffer.timestamp == 0)
            buffer.timestamp = QDateTime::currentMSecsSinceEpoch();
        if (!buffer.slices.contains(sliceId)) {
            buffer.slices[sliceId] = payload;
            buffer.receivedSize += payload.size();
        }

        if (buffer.receivedSize < buffer.totalSize) continue;

        QList<int> keys = buffer.slices.keys();
        std::sort(keys.begin(), keys.end());

        QByteArray frameData;
        for (int k : keys)
            frameData.append(buffer.slices[k]);
        frameBuffers.remove(frameId);

        // NALU 提取
        QList<QByteArray> nals;
        int start = -1;
        for (int i = 0; i < frameData.size() - 2; ++i) {
            if (frameData[i] == 0 && frameData[i+1] == 0 && frameData[i+2] == 1) {
                int offset = (i > 0 && frameData[i-1] == 0) ? 1 : 0;
                int realStart = i - offset;
                if (start != -1)
                    nals.append(frameData.mid(start, realStart - start));
                start = realStart;
            }
        }
        if (start != -1)
            nals.append(frameData.mid(start));

        QByteArray decodePayload;
        bool hasIDR = false;
        for (const QByteArray &nal : nals) {
            int offset = (nal.startsWith(QByteArray::fromHex("00000001"))) ? 4 : 3;
            if (nal.size() <= offset) continue;
            int type = (nal[offset] >> 1) & 0x3F;
            if (type == 32) vps = nal;
            else if (type == 33) sps = nal;
            else if (type == 34) pps = nal;
            else if (type == 19 || type == 20) hasIDR = true;
            decodePayload.append(nal);
        }

        QByteArray finalPacket;
        if (hasIDR && !vps.isEmpty() && !sps.isEmpty() && !pps.isEmpty()) {
            finalPacket = vps + sps + pps + decodePayload;
        } else {
            finalPacket = decodePayload;
        }
        if (!finalPacket.isEmpty()) {
            QMutexLocker locker(&decodeMutex);
            decodeQueue.enqueue(finalPacket);
            decodeCond.wakeOne();
        }
    }
}

void Play::decodeLoop()
{
    while (decodeRunning) {
        QByteArray frameData;
        {
            QMutexLocker locker(&decodeMutex);
            while (decodeQueue.isEmpty() && decodeRunning)
                decodeCond.wait(&decodeMutex);
            if (!decodeRunning) break;
            frameData = decodeQueue.dequeue();
        }

        AVPacket* packet = av_packet_alloc();
        if (!packet) continue;
        if (av_new_packet(packet, frameData.size()) >= 0) {
            memcpy(packet->data, frameData.data(), frameData.size());
            if (avcodec_send_packet(codecCtx, packet) == 0) {
                while (avcodec_receive_frame(codecCtx, frame) == 0) {
                    int w = frame->width;
                    int h = frame->height;
                    if (!swsCtx || videoWidth != w || videoHeight != h) {
                        if (swsCtx) sws_freeContext(swsCtx);
                        swsCtx = sws_getContext(w, h, (AVPixelFormat)frame->format,
                                                w, h, AV_PIX_FMT_RGB24,
                                                SWS_BILINEAR, NULL, NULL, NULL);
                        videoWidth = w;
                        videoHeight = h;
                        rgbBufferSize = w * h * 3;
                        if (rgbBuffer) free(rgbBuffer);
                        rgbBuffer = (uint8_t*)malloc(rgbBufferSize);
                    }
                    uint8_t* rgb[1] = { rgbBuffer };
                    int linesize[1] = { w * 3 };
                    sws_scale(swsCtx, frame->data, frame->linesize, 0, h, rgb, linesize);
                    QImage img(rgbBuffer, w, h, linesize[0], QImage::Format_RGB888);
                    QMetaObject::invokeMethod(this, [this, img]() {
                        renderImage(img);
                    }, Qt::QueuedConnection);
                }
            }
        }
        av_packet_free(&packet);
    }
}

void Play::renderImage(const QImage &img)
{
    if (ui->VideoWidget && !ui->VideoWidget->isHidden()) {
        VideoOpenGLWidget* videoWidget = static_cast<VideoOpenGLWidget*>(ui->VideoWidget);
        videoWidget->setFrame(img);
    }
}

void Play::onGameStatusReceived(const QMqttMessage &message)
{
    ui->connectStateLabel->setText("Status received!");
    QByteArray payload = message.payload();
    GameStatus status;
    if (!status.ParseFromArray(payload.constData(), payload.size())) {
        qDebug() << "Failed to parse GameStatus";
        ui->connectStateLabel->setText("Parse failed");
        return;
    }
    emit gameStatusReceived(status);
}

void Play::updateGameStatusDisplay(const GameStatus &status)
{
    const char* stageText[] = {"未开始", "准备阶段", "自检", "倒计时", "比赛中", "结算"};
    uint32_t stage = status.current_stage();
    if (stage <= 5)
        ui->gameStatus->setText(QString("阶段: %1").arg(stageText[stage]));
    else
        ui->gameStatus->setText("阶段: 未知");
    ui->gameTime->setText(QString("剩余: %1 s").arg(status.stage_remain_time()));
}

void Play::onGlobalUnitStatusReceived(const QMqttMessage &message)
{
    ui->connectStateLabel->setText("Global received!");
    QByteArray payload = message.payload();
    GlobalUnitStatus status;
    if (!status.ParseFromArray(payload.constData(), payload.size())) {
        qDebug() << "Failed to parse GlobalUnitStatus";
        ui->connectStateLabel->setText("Parse failed");
        return;
    }
    emit globalUnitStatusReceived(status);
}

void Play::updateGlobalUnitDisplay(const GlobalUnitStatus &status)
{
    ui->allyBaseHP->setText(QString("基地血量: %1").arg(status.base_health()));
    ui->enemyBaseHP->setText(QString("敌方基地: %1").arg(status.enemy_base_health()));
}

void Play::sendAirSupportCommand()
{
    if (!mqttClient || mqttClient->state() != QMqttClient::Connected) {
        updateAirSupportStatus("MQTT未连接");
        return;
    }
    AirSupportCommand cmd;
    cmd.set_command_id(1);
    std::string serialized;
    if (!cmd.SerializeToString(&serialized)) {
        qDebug() << "序列化 AirSupportCommand 失败";
        updateAirSupportStatus("指令序列化失败");
        return;
    }
    QByteArray payload(serialized.c_str(), static_cast<int>(serialized.size()));
    mqttClient->publish(QMqttTopicName("AirSupportCommand"), payload);
    updateAirSupportStatus("空中支援请求已发送");
}

void Play::onAirSupportStatusSyncReceived(const QByteArray &payload)
{
    AirSupportStatusSync status;
    if (!status.ParseFromArray(payload.constData(), payload.size())) {
        qDebug() << "解析 AirSupportStatusSync 失败";
        updateAirSupportStatus("解析状态失败");
        return;
    }
    QString stateText = (status.airsupport_status() == 0) ? "空闲" : "空中支援进行中";
    QString info = QString("空中支援: %1 | 剩余时间: %2秒 | 反制: %3")
                       .arg(stateText)
                       .arg(status.left_time())
                       .arg(status.shooter_status() == 0 ? "锁定" : "正常");
    if (status.laser_detection_status())
        info += " | 被照射中";
    updateAirSupportStatus(info);
}

void Play::updateAirSupportStatus(const QString &status)
{
    if (ui->airSupportStatusLabel) {
        ui->airSupportStatusLabel->setText(status);
        if (status.contains("进行中") || status.contains("请求已发送"))
            ui->airSupportStatusLabel->setStyleSheet("color: orange; font-weight: bold;");
        else if (status.contains("空闲"))
            ui->airSupportStatusLabel->setStyleSheet("color: green; font-weight: bold;");
        else
            ui->airSupportStatusLabel->setStyleSheet("color: white;");
    }
}

void Play::onBuffStatusReceived(const QByteArray &payload)
{
    Buff buffMsg;
    if (!buffMsg.ParseFromArray(payload.constData(), payload.size())) {
        qDebug() << "解析 Buff 消息失败";
        ui->buffLabel->setText("Buff: 解析错误");
        return;
    }
    QString buffTypeStr;
    switch (buffMsg.buff_type()) {
    case 1: buffTypeStr = "攻击增益"; break;
    case 2: buffTypeStr = "防御增益"; break;
    case 3: buffTypeStr = "冷却增益"; break;
    case 4: buffTypeStr = "功率增益"; break;
    case 5: buffTypeStr = "回血增益"; break;
    case 6: buffTypeStr = "可兑换发弹量"; break;
    case 7: buffTypeStr = "地形跨越预备"; break;
    default: buffTypeStr = QString("未知(%1)").arg(buffMsg.buff_type()); break;
    }
    QString displayText = QString("Buff: %1 | 强度: %2 | 剩余: %3秒")
                              .arg(buffTypeStr)
                              .arg(buffMsg.buff_level())
                              .arg(buffMsg.buff_left_time());
    ui->buffLabel->setText(displayText);
    if (buffMsg.buff_type() == 1)
        ui->buffLabel->setStyleSheet("color: red;");
    else if (buffMsg.buff_type() == 2)
        ui->buffLabel->setStyleSheet("color: green;");
    else
        ui->buffLabel->setStyleSheet("color: white;");
}

void Play::onRobotRealtimeReceived(const QByteArray &payload)
{
    RobotDynamicStatus status;
    if (!status.ParseFromArray(payload.constData(), payload.size())) {
        qDebug() << "解析 RobotDynamicStatus 失败";
        return;
    }
    ui->robotHealthLabel->setText(QString("血量: %1 / ?").arg(status.current_health()));
    ui->robotHeatLabel->setText(QString("热量: %1").arg(status.current_heat()));
    ui->robotRemainingAmmoLabel->setText(QString("剩余弹量: %1").arg(status.remaining_ammo()));
}
