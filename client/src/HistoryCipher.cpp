#include "HistoryCipher.h"

#include "../third_party/monocypher/monocypher.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace {

// Format marker; bump the digit if the record layout ever changes.
const char kMagic[4] = {'S', 'G', 'E', '1'};
constexpr int kMagicLen = 4;
constexpr int kNonceLen = 24;
constexpr int kMacLen   = 16;
constexpr int kKeyLen   = 32;

QByteArray systemRandomBytes(int n) {
    QByteArray out(n, Qt::Uninitialized);
    // QRandomGenerator::system() draws from the OS CSPRNG
    // (/dev/urandom, BCryptGenRandom, ...).
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32 *>(out.data()), n / int(sizeof(quint32)));
    for (int i = n - n % int(sizeof(quint32)); i < n; ++i)
        out[i] = char(QRandomGenerator::system()->bounded(256));
    return out;
}

#ifdef Q_OS_WIN
QByteArray dpapiWrap(const QByteArray &in, QString *error) {
    DATA_BLOB src{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.data()))};
    DATA_BLOB dst{};
    if (!CryptProtectData(&src, L"family-signal history key", nullptr, nullptr,
                          nullptr, 0, &dst)) {
        if (error) *error = QStringLiteral("CryptProtectData failed");
        return {};
    }
    QByteArray out(reinterpret_cast<char *>(dst.pbData), int(dst.cbData));
    LocalFree(dst.pbData);
    return out;
}

QByteArray dpapiUnwrap(const QByteArray &in, QString *error) {
    DATA_BLOB src{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.data()))};
    DATA_BLOB dst{};
    if (!CryptUnprotectData(&src, nullptr, nullptr, nullptr, nullptr, 0, &dst)) {
        if (error) *error = QStringLiteral("CryptUnprotectData failed");
        return {};
    }
    QByteArray out(reinterpret_cast<char *>(dst.pbData), int(dst.cbData));
    LocalFree(dst.pbData);
    return out;
}
#endif

} // namespace

bool HistoryCipher::init(const QString &dir) {
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("history.key"));

    QFile f(path);
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly)) {
            m_error = QStringLiteral("cannot read %1").arg(path);
            return false;
        }
        QByteArray raw = f.readAll();
#ifdef Q_OS_WIN
        raw = dpapiUnwrap(raw, &m_error);
        if (raw.isEmpty()) return false;
#endif
        if (raw.size() != kKeyLen) {
            m_error = QStringLiteral("%1 is corrupt (size %2)").arg(path).arg(raw.size());
            return false;
        }
        m_key = raw;
        return true;
    }

    m_key = systemRandomBytes(kKeyLen);
    QByteArray stored = m_key;
#ifdef Q_OS_WIN
    stored = dpapiWrap(stored, &m_error);
    if (stored.isEmpty()) return false;
#endif
    if (!f.open(QIODevice::WriteOnly)) {
        m_error = QStringLiteral("cannot create %1").arg(path);
        return false;
    }
    f.write(stored);
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QByteArray HistoryCipher::encrypt(const QString &plainText) const {
    if (m_key.size() != kKeyLen) return {};
    const QByteArray plain = plainText.toUtf8();
    const QByteArray nonce = systemRandomBytes(kNonceLen);

    QByteArray out(kMagicLen + kNonceLen + kMacLen + plain.size(), Qt::Uninitialized);
    memcpy(out.data(), kMagic, kMagicLen);
    memcpy(out.data() + kMagicLen, nonce.constData(), kNonceLen);

    auto *mac    = reinterpret_cast<uint8_t *>(out.data() + kMagicLen + kNonceLen);
    auto *cipher = reinterpret_cast<uint8_t *>(out.data() + kMagicLen + kNonceLen + kMacLen);
    crypto_aead_lock(cipher, mac,
                     reinterpret_cast<const uint8_t *>(m_key.constData()),
                     reinterpret_cast<const uint8_t *>(nonce.constData()),
                     nullptr, 0,
                     reinterpret_cast<const uint8_t *>(plain.constData()),
                     size_t(plain.size()));
    return out;
}

QString HistoryCipher::decrypt(const QByteArray &blob) const {
    if (m_key.size() != kKeyLen || !looksEncrypted(blob)) return {};
    const int ctLen = blob.size() - kMagicLen - kNonceLen - kMacLen;

    const auto *nonce  = reinterpret_cast<const uint8_t *>(blob.constData() + kMagicLen);
    const auto *mac    = reinterpret_cast<const uint8_t *>(blob.constData() + kMagicLen + kNonceLen);
    const auto *cipher = reinterpret_cast<const uint8_t *>(blob.constData() + kMagicLen + kNonceLen + kMacLen);

    QByteArray plain(ctLen, Qt::Uninitialized);
    if (crypto_aead_unlock(reinterpret_cast<uint8_t *>(plain.data()), mac,
                           reinterpret_cast<const uint8_t *>(m_key.constData()),
                           nonce, nullptr, 0, cipher, size_t(ctLen)) != 0)
        return {};
    return QString::fromUtf8(plain);
}

bool HistoryCipher::looksEncrypted(const QByteArray &blob) {
    return blob.size() >= kMagicLen + kNonceLen + kMacLen &&
           memcmp(blob.constData(), kMagic, kMagicLen) == 0;
}
