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
class QStackedWidget;
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
    void onCallConnectionInterrupted();
    void onCallConnectionRestored();
    void onCallConnectionFailed();

    void onTextReceived(const QString &fromPeerId, const QString &msgId,
                        const QString &text);
    void onTextDelivered(const QString &msgId);

    void onStartVideoCall();
    void onStartAudioCall();
    void onAcceptIncomingCall();
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
    WebRtcSession *openChannelTo(const QString &peerId) const;
    // Mark the on-screen conversation as read and push receipts to the peer.
    void sendReadReceipts(const QString &peerId, bool markConversation);
    void onPeerReadMessages(const QStringList &msgIds);

    void startOutgoingCall(bool withVideo);
    CallWindow *ensureCallWindow();

    // History paging: 50 messages at a time, older pages pulled in as the
    // user scrolls towards the top of the conversation.
    void loadOlderMessages();
    bool m_olderExhausted = false;   // no more rows for the current dialog
    bool m_loadingOlder   = false;   // re-entrancy guard for scroll storms

    // Incoming call waiting for the user to press "Принять" — media is NOT
    // running yet. Cleared on accept/decline/bye.
    struct PendingOffer {
        QString peerId;
        QString sdp;
        bool    video = true;
    } m_pendingOffer;
    bool m_outgoingVideo = true;   // kind for offers created by m_webrtc

    // Active-call bookkeeping for the Telegram-style call log pills.
    void logCallEvent(const QString &peerId, const QString &text, bool bad);
    QString   m_activeCallPeer;
    bool      m_activeCallVideo = true;
    QDateTime m_callConnectedAt;   // invalid until the call connects

    // Dropped-call recovery (flaky networks). The original caller redials
    // automatically; the callee auto-accepts the caller's redial within the
    // grace window so nobody has to press "Принять" a second time.
    void attemptRedial(const QString &peerId, bool video, int triesLeft);
    int       m_redialAttempts = 0;   // reset on connect / manual call
    QString   m_autoAcceptPeer;       // redial from them is answered silently
    QDateTime m_autoAcceptUntil;

    Config           m_config;
    SignalingClient *m_signaling   = nullptr;
    WebRtcSession   *m_webrtc      = nullptr;
    MessageHistory  *m_history     = nullptr;
    QHash<QString, WebRtcSession *> m_chatSessions;   // peerId → silent session

    ContactsModel   *m_contactsModel = nullptr;
    ChatModel       *m_chatModel     = nullptr;

    QSplitter       *m_splitter     = nullptr;
    QStackedWidget  *m_rightStack   = nullptr;
    QListView       *m_contactsView = nullptr;
    ChatHeader      *m_chatHeader   = nullptr;
    ChatView        *m_chatView     = nullptr;
    MessageInputBar *m_inputBar     = nullptr;
    CallWindow      *m_callWindow   = nullptr;

    Contact          m_currentContact;
};
