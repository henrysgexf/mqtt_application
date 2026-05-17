#include "hevcnalparser.h"


HevcNalParser::HevcNalParser()
{
}

bool HevcNalParser::isReady() const
{
    return !vps.isEmpty() && !sps.isEmpty() && !pps.isEmpty();
}

void HevcNalParser::appendData(const QByteArray &data)
{
    streamBuffer.append(data);
}

// 找所有起始码并切NAL
QList<QByteArray> HevcNalParser::splitNals()
{
    QList<QByteArray> nals;

    int start = -1;
    int i = 0;

    while (i < streamBuffer.size() - 4) {

        bool isStart =
            (streamBuffer[i] == 0x00 && streamBuffer[i+1] == 0x00 &&
             ((streamBuffer[i+2] == 0x01) ||
              (streamBuffer[i+2] == 0x00 && streamBuffer[i+3] == 0x01)));

        if (isStart) {

            if (start != -1) {
                nals.append(streamBuffer.mid(start, i - start));
            }

            start = i;

            if (streamBuffer[i+2] == 0x01)
                i += 3;
            else
                i += 4;
        } else {
            i++;
        }
    }

    // 保留未完成部分
    if (start != -1) {
        streamBuffer = streamBuffer.mid(start);
    } else {
        streamBuffer.clear();
    }

    return nals;
}

// 获取NAL类型
int HevcNalParser::getNalType(const QByteArray &nal)
{
    if (nal.size() < 5) return -1;
    return (nal[4] >> 1) & 0x3F;
}

// 主处理函数
QByteArray HevcNalParser::processFrame(const QByteArray &frameData)
{
    QList<QByteArray> nals = splitNals();

    QByteArray output;
    bool hasIDR = false;

    for (const QByteArray &nal : nals) {

        int type = getNalType(nal);

        if (type == 32) {
            vps = nal;
        }
        else if (type == 33) {
            sps = nal;
        }
        else if (type == 34) {
            pps = nal;
        }
        else if (type == 19 || type == 20) {
            hasIDR = true;
        }

        output.append(nal);
    }

    // 如果是关键帧，补齐参数集
    if (hasIDR) {
        QByteArray full;

        if (!vps.isEmpty()) full.append(vps);
        if (!sps.isEmpty()) full.append(sps);
        if (!pps.isEmpty()) full.append(pps);

        full.append(output);
        return full;
    }

    return output;
}
