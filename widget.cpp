#include "widget.h"
#include "ui_widget.h"
#include "play.h"
#include <QMessageBox>
#include <QDebug>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , mqttClient(nullptr)
    , playWindow(nullptr)
    , disconnectDelayTimer(nullptr)
    , disconnectDelayMs(3000)   // 默认3秒
{
    ui->setupUi(this);

    // 创建 MQTT 客户端实例
    mqttClient = new QMqttClient(this);

    // 创建断连延迟定时器
    disconnectDelayTimer = new QTimer(this);
    disconnectDelayTimer->setSingleShot(true);

    // 连接 MQTT 信号
    connect(mqttClient, &QMqttClient::connected, this, &Widget::onMqttConnected);
    connect(mqttClient, &QMqttClient::disconnected, this, &Widget::onMqttDisconnected);
    connect(disconnectDelayTimer, &QTimer::timeout, this, &Widget::onDisconnectDelayTimeout);

    // // 如果 UI 中存在用于设置延迟的 SpinBox，连接其信号（用户需在 .ui 中添加对象名为 delaySpinBox 的 QSpinBox）
    // QSpinBox *delaySpinBox = ui->delaySpinBox;
    // if (delaySpinBox) {
    //     delaySpinBox->setValue(disconnectDelayMs);
    //     connect(delaySpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
    //             this, &Widget::setDisconnectDelay);
    // }
}

Widget::~Widget()
{
    delete ui;
}

// void Widget::setDisconnectDelay(int msec)
// {
//     if (msec >= 0)
//         disconnectDelayMs = msec;
// }

void Widget::onMqttConnected()
{
    qDebug() << "MQTT connected, cancel any pending disconnect delay";
    // 取消正在进行的断连延迟
    if (disconnectDelayTimer->isActive())
        disconnectDelayTimer->stop();

    // 如果 Play 窗口已存在且有效，则直接隐藏本窗口，不重复创建
    if (playWindow && !playWindow->isHidden()) {
        this->hide();
        return;
    }

    // 否则，先销毁旧的 Play 窗口（如果有），再创建新的
    if (playWindow) {
        playWindow->close();
        playWindow->deleteLater();
        playWindow = nullptr;
    }

    playWindow = new Play(mqttClient);
    playWindow->setupMqtt();   // 订阅主题
    playWindow->show();
    this->hide();
    ui->statusLabel->setText("连接成功");
}

void Widget::onMqttDisconnected()
{
    qDebug() << "MQTT disconnected, start delay timer for" << disconnectDelayMs << "ms";
    // 启动断连延迟定时器，等待一段时间确认是否真正断连
    if (!disconnectDelayTimer->isActive())
        disconnectDelayTimer->start(disconnectDelayMs);
    ui->statusLabel->setText("连接断开，等待重连...");
}

void Widget::onDisconnectDelayTimeout()
{
    // 再次检查连接状态，如果已经恢复则不做任何处理
    if (mqttClient->state() == QMqttClient::Connected) {
        qDebug() << "Connection recovered during delay, keep Play window";
        return;
    }
    this->show();
    qDebug() << "Disconnect delay timeout, killing Play window and resetting MQTT client";

    // 1. 销毁 Play 窗口（停止其解码线程等）
    if (playWindow) {
        playWindow->close();
        playWindow->deleteLater();
        playWindow = nullptr;
    }

    // 2. 清除旧的 MQTT 客户端
    if (mqttClient) {
        mqttClient->disconnectFromHost();
        mqttClient->deleteLater();
        mqttClient = nullptr;
    }

    // 3. 创建全新的 MQTT 客户端实例
    mqttClient = new QMqttClient(this);
    connect(mqttClient, &QMqttClient::connected, this, &Widget::onMqttConnected);
    connect(mqttClient, &QMqttClient::disconnected, this, &Widget::onMqttDisconnected);

    // 4. 使用上次成功连接的参数自动重连（如果已保存）
    if (!lastHost.isEmpty()) {
        mqttClient->setHostname(lastHost);
        mqttClient->setPort(lastPort);
        mqttClient->setClientId(lastClientId);
        if (!lastUsername.isEmpty())
            mqttClient->setUsername(lastUsername);
        if (!lastPassword.isEmpty())
            mqttClient->setPassword(lastPassword.toUtf8());

        qDebug() << "Auto reconnecting to" << lastHost << ":" << lastPort;
        mqttClient->connectToHost();
        ui->statusLabel->setText("自动重连中...");
    } else {
        // 没有保存连接参数，显示连接窗口等待用户操作
        this->show();
        ui->statusLabel->setText("连接断开，请手动连接");
    }
}

void Widget::on_connectBtn_clicked()
{
    QString host = ui->ipEdit->text();
    quint16 port = ui->portEdit->text().toUShort();
    QString clientId = ui->clientIdEdit->text();
    QString username = ui->userEdit->text();
    QString password = ui->passwordEdit->text();

    if (host.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入服务器IP");
        return;
    }

    // 保存连接参数，用于后续自动重连
    lastHost = host;
    lastPort = port;
    lastClientId = clientId;
    lastUsername = username;
    lastPassword = password;

    mqttClient->setHostname(host);
    mqttClient->setPort(port);
    mqttClient->setClientId(clientId);
    if (!username.isEmpty())
        mqttClient->setUsername(username);
    if (!password.isEmpty())
        mqttClient->setPassword(password.toUtf8());

    if (mqttClient->state() != QMqttClient::Connected) {
        mqttClient->connectToHost();
    }
    ui->statusLabel->setText("连接中...");
}
