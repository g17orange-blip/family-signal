#pragma once

#include "Config.h"
#include "Contact.h"
#include "Message.h"

#include <QHash>
#include <QMainWindow>

class CallWindow;
class ChatHeader;
class ChatModel;
class ChatView;
class ContactsModel;
class MessageHistory;
class MessageInputBar;
class QListView;
class QSplitter;
class SignalingClient;
class WebRtcSession;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Ensures a usable config exists: loads config.json, or — on first launch
    // when none exists — runs the setup wizard (FirstRunDialog) to create one.
    // Returns false if the user cancels the wizard or the config is invalid.
    bool ensureConfigured();
    void start();

    // Demo mode: skips signaling and GStreamer entirely, seeds a fake
    // online contact and sample chat history so the UI can be evaluated
    // without any server infrastructure or camera permissions.
    void startDemo();

private slots:
    void onSignalingConnected();
    void onSignalingDisconnected();
    void onSignalingError(const QString &reason);
    void onPresenceChanged(const QStringList &online);

    void onContactSelected(const QModelIndex &index);
    void onSendText(const QString &text);

    void onIncomingOffer(const QString &fromPeerId, const QString &sdp,
                         const QString &kind);
    void onIncomingAnswer(const QString &fromPeerId, const QString &sdp,
                          const QString &kind);
    void onIncomingIce(const QString &fromPeerId, const QString &candidate,
                       const QString &sdpMid, int sdpMLineIndex,
                       const QString &kind);
    void onIncomingBye(const QString &fromPeerId);

    void onLocalOffer(const QString &peerId, const QString &sdp);
    void onLocalAnswer(const QString &peerId, const QString &sdp);
    void onLocalIce(const QString &peerId, const QString &candidate,
                    const QString &sdpMid, int sdpMLineIndex);
    void onCallConnected();
    void onCallEnded();
    void onWebRtcError(const QString &message);

    void onTextReceived(const QString &fromPeerId, const QString &msgId,
                        const QString &text);
    void onTextDelivered(const QString &msgId);

    void onStartVideoCall();
    void onStartAudioCall();
    void onHangupRequested();

    void onOpenSettings();

private:
    void selectContactById(const QString &id);
    Message appendMessage(const QString &peerId, const QString &senderId,
                          const QString &text, const QString &msgId = QString());
    Contact currentContact() const;

    // --- Background chat sessions (offline queue delivery) -----------------
    // One silent, DataChannel-only WebRTC session per online peer. To avoid
    // both sides offering at once, only the lexicographically smaller userId
    // initiates. Queued (undelivered) messages flush when a channel opens.
    WebRtcSession *ensureChatSession(const QString &peerId);
    void syncChatSessions(const QStringList &onlinePeers);
    void flushQueuedMessages(const QString &peerId);
    bool sendViaAnyChannel(const QString &peerId, const QString &msgId,
                           const QString &text);

    Config           m_config;
    SignalingClient *m_signaling   = nullptr;
    WebRtcSession   *m_webrtc      = nullptr;
    MessageHistory  *m_history     = nullptr;
    QHash<QString, WebRtcSession *> m_chatSessions;   // peerId → silent session

    ContactsModel   *m_contactsModel = nullptr;
    ChatModel       *m_chatModel     = nullptr;

    QSplitter       *m_splitter     = nullptr;
    QListView       *m_contactsView = nullptr;
    ChatHeader      *m_chatHeader   = nullptr;
    ChatView        *m_chatView     = nullptr;
    MessageInputBar *m_inputBar     = nullptr;
    CallWindow      *m_callWindow   = nullptr;

    Contact          m_currentContact;
};
