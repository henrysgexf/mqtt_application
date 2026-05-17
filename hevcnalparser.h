#pragma once
#include <QByteArray>
#include <QList>

class HevcNalParser
{
public:
    HevcNalParser();

    // 输入一帧数据，输出可解码数据（可能为空）
    QByteArray processFrame(const QByteArray &frameData);
    QList<QByteArray> splitNals();
    bool isReady() const;



    void appendData(const QByteArray &data);

private:
    int getNalType(const QByteArray &nal);

private:
    QByteArray streamBuffer;
    QByteArray vps;
    QByteArray sps;
    QByteArray pps;


};
