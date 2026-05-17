#include "widget.h"
#include "ui_widget.h"
#include "play.h"

#include <QMessageBox>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    mqttClient = new QMqttClient(this);

    connect(mqttClient,
            &QMqttClient::connected,
            this,
            &Widget::onMqttConnected);
}

Widget::~Widget()
{
    delete ui;
}


void Widget::onMqttConnected()
{
    ui->statusLabel->setText("连接成功");
    // if (playWindow) {
    //     playWindow->close();
    //     playWindow->deleteLater();}


    playWindow = new Play(mqttClient);
    playWindow->show();
    playWindow->setupMqtt();  // 关键：连接后立即设置订阅

    this->hide();
}




void Widget::on_connectBtn_clicked()
{
    QString host = ui->ipEdit->text();
    quint16 port = ui->portEdit->text().toUShort();
    QString clientId = ui->clientIdEdit->text();
    QString username = ui->userEdit->text();
    QString password = ui->passwordEdit->text();


    if(host.isEmpty())
    {
        QMessageBox::warning(this,"错误","请输入服务器IP");
        return;
    }

    mqttClient->setHostname(host);
    mqttClient->setPort(port);
    mqttClient->setClientId(clientId);
    if(!username.isEmpty())
        mqttClient->setUsername(username);
    if(!password.isEmpty())
        mqttClient->setPassword(password.toUtf8());

    if(mqttClient->state() != QMqttClient::Connected)
    {
        mqttClient->connectToHost();
    }

    ui->statusLabel->setText("连接中...");
}

