/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "Fingerprint.h"

#include <openssl/x509.h>

namespace deskflow {

/**
 * @brief formatSSLFingerprint Format an ssl Fingerprint
 * @param fingerprint input string
 * @param enableSeparators insert : seperator every byte when true
 * @return a Formated Fingerprint String
 */
QString formatSSLFingerprint(const QByteArray &fingerprint, bool enableSeparators = true);

QString formatSSLFingerprintColumns(const QByteArray &fingerprint);

Fingerprint sslCertFingerprint(const X509 *cert, QCryptographicHash::Algorithm type);

//! @param commonName the computer's name: peers show it when asked to trust
//! this certificate
void generatePemSelfSignedCert(const QString &path, int keyLength = 2048, const QString &commonName = {});

//! The common name of a certificate's subject, or nothing for the generic
//! name of certificates made before it carried the computer's name
QString sslCertComputerName(const X509 *cert);

QString generateFingerprintArt(const QByteArray &rawDigest);
} // namespace deskflow
