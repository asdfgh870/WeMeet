﻿#include "chatwindow.h"
#include "ui_chatwindow.h"
#include "src/tcpclient.h"
#include <src/cameraVideo.h>
#include "src/config.h"
#include <QDateTime>
#include <src/protoc/message.pb.h>

#include <QFileDialog>
#include <QFileIconProvider>
#include <QMessageBox>

#include <ui/chatmessage/messagecommon.h>
#include <ui/chatmessage/timemessage.h>
#include <ui/chatmessage/textmessage.h>
#include <ui/chatmessage/filemessage.h>

#include <opencv2/opencv.hpp>

ChatWindow::ChatWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ChatWindow),
    m_chatClient(new TcpClient(QString::fromStdString(Config::getInstance().get("server_address")),
                               stoi(Config::getInstance().get("server_port")),
                               parent))
{
    ui->setupUi(this);
    ui->splitter->handle(1)->setAttribute(Qt::WA_Hover, true);
    ui->listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // construct CameraVideo after ui->setupUi
    m_cameraVideo = new CameraVideo(ui->displayWidget, 30, 0, parent),
    m_chatClient->start();
    connectEventSlotsOrCallbacks();
}

ChatWindow::~ChatWindow()
{
    delete ui;
}

void ChatWindow::sendTextMessage(const QString &msg)
{
    MeetChat::Message message;
    message.set_sender_id(Config::getInstance().get("userId"));
    message.set_receiver_id("-1");
    message.mutable_timestamp()->set_seconds(QDateTime::currentSecsSinceEpoch());
    message.set_type(MeetChat::MessageType::TEXT);

    MeetChat::TextMessage text_message;
    std::string stdstr = msg.toStdString();
    text_message.set_text(stdstr);

    google::protobuf::Any *any = new google::protobuf::Any;
    any->PackFrom(text_message);
    message.set_allocated_data(any);

    m_chatClient->send(message);
}

void ChatWindow::sendFileMessage(const QString &file_path)
{
    QFile file(file_path);
    QFileInfo fileInfo(file.fileName());
    QString fileName = fileInfo.fileName();
    file.open(QIODevice::ReadOnly);
    if(file.size() >= (long long)2 * 1024 * 1024 * 1024) // 2GB
    {
        qDebug() << "Too big, ignore file";
        QMessageBox msgBox;
        msgBox.setWindowTitle(Chinese("警告"));
        msgBox.setText(Chinese("无法发送文件，文件太大 ( > 2GB )"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec(); // wait user to click
        return ;
    }
    // file.readAll() only reads data less that 2GB
    std::string data(file.readAll().toStdString());

    MeetChat::File file_message;
    file_message.set_name(fileName.toStdString());
    file_message.set_size(file.size());
    assert(file.size() == data.size());
    file_message.set_data(data.data(), file.size());

    google::protobuf::Any *any = new google::protobuf::Any;
    bool f = any->PackFrom(file_message);
    if(!f){
        qDebug() << "packfrom error";
        return ;
    }

    MeetChat::Message message;
    message.set_sender_id(Config::getInstance().get("userId"));
    message.set_receiver_id("all");
    message.mutable_timestamp()->set_seconds(QDateTime::currentSecsSinceEpoch());
    message.set_type(MeetChat::MessageType::FILE);
    message.set_allocated_data(any);

    m_chatClient->send(message);
}

void ChatWindow::sendAVMessgae(const MeetChat::AVPacket &av_packet)
{
    MeetChat::Message message;
    message.set_sender_id(Config::getInstance().get("userId"));
    message.set_receiver_id("all");
    message.mutable_timestamp()->set_seconds(QDateTime::currentSecsSinceEpoch());
    message.set_type(MeetChat::MessageType::AV);

    google::protobuf::Any *any = new google::protobuf::Any;
    any->PackFrom(av_packet);
    message.set_allocated_data(any);

    m_chatClient->send(message);
}

void ChatWindow::onProtoMessageReceived(ProtoMessagePtr baseMessage)
{
    QSharedPointer<MeetChat::Message> message = baseMessage.dynamicCast<MeetChat::Message>();
    switch (message->type()) {
        case MeetChat::MessageType::TEXT:
            onTextMessage(message);
            break;
        case MeetChat::MessageType::FILE:
            onFileMessage(message);
            break;
        case MeetChat::MessageType::AV:
            onAVMessage(message);
            break;
        default:
            std::cerr << "Unknown message type\n";
        }
}

void ChatWindow::onTextMessage(const QSharedPointer<MeetChat::Message> &message)
{
    assert(message->type() == MeetChat::TEXT);
    google::protobuf::Any any = message->data();
    MeetChat::TextMessage text_message;
    if (any.UnpackTo(&text_message)) {
        std::string text = text_message.text();
        qDebug() << QString::fromStdString(text);
        displayTextMessage(QString::fromStdString(message->sender_id()),
                           QString::fromStdString(text),
                           QDateTime::currentSecsSinceEpoch(),
                           TextMessage::OTHER);
    } else {
        // can not parse
        std::cerr << "Can`t parse data to TEXT type\n";
    }
}

void ChatWindow::onFileMessage(const QSharedPointer<MeetChat::Message>& message)
{
    assert(message->type() == MeetChat::FILE);
    google::protobuf::Any any = message->data();
    MeetChat::File file_message;
    if (any.UnpackTo(&file_message)) {
        qDebug() << "received file name: " << file_message.name().c_str();

        QFile savefile(QString::fromStdString("./tmp/" + file_message.name()));
        QDir dir("./tmp");
        if (!dir.exists()) {
            dir.mkpath("./");
        }
        savefile.open(QIODevice::WriteOnly);
        savefile.write(file_message.data().data(), file_message.size());

        QFileInfo file_info(QString::fromStdString(file_message.name()));
        QFileIconProvider icon_provider;
        QIcon icon = icon_provider.icon(file_info);
        displayFileMessage(QString::fromStdString(message->sender_id()),
                           icon.pixmap(10,10),
                           file_info.fileName(),
                           file_message.size(),
                           QDateTime::currentSecsSinceEpoch(),
                           FileMessage::OTHER);
    } else {
        std::cerr << "Can`t parse data to FILE type\n";
    }
}

void ChatWindow::onAVMessage(const QSharedPointer<MeetChat::Message>& message)
{
    assert(message->type() == MeetChat::AV);

    this->m_cameraVideo->decodeAVPacket(message);
}

void ChatWindow::displayTextMessage(const QString& title,
                        const QString& text,
                        qint64 timeSecsSinceUnixEpoch,
                        TextMessage::TYPE userType)
{
    mayDisplayTimeMessage(timeSecsSinceUnixEpoch);

    TextMessage* message = new TextMessage(userType, ui->listWidget);
    QListWidgetItem* itemTime = new QListWidgetItem(ui->listWidget);

    message->setContent(text);
    message->setTitle(title);
    message->setMsgTime(timeSecsSinceUnixEpoch);

    QSize size = QSize(this->ui->listWidget->width(), message->height());
    message->resize(size);
    itemTime->setSizeHint(size);
    ui->listWidget->setItemWidget(itemTime, message);

}

void ChatWindow::on_sendButton_clicked()
{
    QString msg = ui->textInput->toPlainText();
    ui->textInput->setText("");
    if(msg.isEmpty()){
        ui->textInput->setPlaceholderText("can not send empty or blank message");
        return ;
    }

    displayTextMessage(QString::fromStdString(Config::getInstance().get("userId")),
                       msg,
                       QDateTime::currentSecsSinceEpoch(),
                       TextMessage::ME);
    sendTextMessage(msg);

}

void ChatWindow::on_fileButton_clicked()
{
    auto filepath = QFileDialog::getOpenFileName(this, tr("Open"), "");
    if(filepath.isEmpty())
        return ;
    // send file
    QFileInfo file_info(filepath);
    QFileIconProvider icon_provider;
    QIcon icon = icon_provider.icon(file_info);
    displayFileMessage("chenl",
                       icon.pixmap(10,10),
                       file_info.fileName(),
                       file_info.size(),
                       QDateTime::currentSecsSinceEpoch(),
                       FileMessage::ME);

    sendFileMessage(filepath);
}

void ChatWindow::on_videoButton_clicked()
{
    static qint64 i = -1; i++;
    // open camera
    if(i % 2 == 0){
        ui->videoButton->setText(QString::fromLocal8Bit("关闭视频"));
        m_cameraVideo->startCap();
    }else{ // close camera
        ui->videoButton->setText(QString::fromLocal8Bit("开启视频"));
        m_cameraVideo->stopCap();
    }
}

void ChatWindow::on_soundButton_clicked()
{
    static qint64 i = -1; i++;
    // open camera
    if(i % 2 == 0){
        ui->soundButton->setText(QString::fromLocal8Bit("关闭声音"));

    }else{ // close camera
        ui->soundButton->setText(QString::fromLocal8Bit("开启声音"));
    }
}

void ChatWindow::displayTimeMessage(qint64 timeSecsSinceEpoch)
{
    TimeMessage* messageTime = new TimeMessage(ui->listWidget);
    messageTime->setTime(timeSecsSinceEpoch);
    QListWidgetItem* itemTime = new QListWidgetItem(ui->listWidget);

    QSize size = QSize(this->ui->listWidget->width(), 30);
    messageTime->resize(size);
    itemTime->setSizeHint(size);
    ui->listWidget->setItemWidget(itemTime, messageTime);
}

void ChatWindow::displayFileMessage(const QString& title,
                                    const QPixmap& fileIcon,
                                    const QString& file_name_str,
                                    qint64 file_size_bytes,
                                    qint64 timeSecsSinceUnixEpoch,
                                    FileMessage::TYPE type)
{
    mayDisplayTimeMessage(timeSecsSinceUnixEpoch);

    FileMessage* message = new FileMessage(type, ui->listWidget);
    QListWidgetItem* itemfile = new QListWidgetItem(ui->listWidget);

    message->setTitle(title);
    message->setIcon(fileIcon);
    message->setFileNameStr(file_name_str);
    message->setFileSize(file_size_bytes);
    message->setMsgTime(timeSecsSinceUnixEpoch);

    QSize size = QSize(message->width(), 60);
    message->resize(size);

    itemfile->setSizeHint(size);
    ui->listWidget->setItemWidget(itemfile, message);
}

void ChatWindow::connectEventSlotsOrCallbacks()
{
    connect(m_chatClient.get(), SIGNAL(protobufMessage(ProtoMessagePtr)),
            this, SLOT(onProtoMessageReceived(ProtoMessagePtr)));
    this->m_cameraVideo->setOnFrameEncodedCallback([this](const MeetChat::AVPacket& data){
        // send to peer
        this->sendAVMessgae(data);
    });
    this->m_cameraVideo->setOnPacketDecodedCallback([this](const MessageContext& ctx, const AVFrame* m_video_frame){
        auto * sws_ctx = this->m_cameraVideo->getSwsCtx();

        // AVFrame --> cv::Mat
        cv::Mat mat(m_video_frame->height, m_video_frame->width, CV_8UC3);
        uint8_t* data[AV_NUM_DATA_POINTERS] = {0};
        data[0] = mat.data;
        int linesize[AV_NUM_DATA_POINTERS] = {0};
        linesize[0] = mat.cols * mat.channels();
        sws_scale(const_cast<SwsContext*>(sws_ctx), m_video_frame->data, m_video_frame->linesize,
                  0, m_video_frame->height, data, linesize);
        // show
        cv::imshow(ctx.getReceiver_id() + " " + ctx.getSender_id() + " ", mat);
        cv::waitKey(1);

    });

}

void ChatWindow::mayDisplayTimeMessage(qint64 curMsgTimeSecsSinceEpoch)
{
    bool showTime = false;
    // compare with previous sended message time
    if(ui->listWidget->count() > 0){
        QListWidgetItem* lastItem = ui->listWidget->item(ui->listWidget->count() - 1);
        MessageCommon* messageW =  dynamic_cast<MessageCommon*>(ui->listWidget->itemWidget(lastItem));
        int lastTimeSecs = messageW->msgTimeSecs();
        int curTimeSecs = curMsgTimeSecsSinceEpoch;
        qDebug() << "curTime lastTime:" << curTimeSecs - lastTimeSecs;
        showTime = ((curTimeSecs - lastTimeSecs) > 60);
    }else{
    // the first message case
        showTime = true;
    }
    if(showTime){
        displayTimeMessage(curMsgTimeSecsSinceEpoch);
    }
}


