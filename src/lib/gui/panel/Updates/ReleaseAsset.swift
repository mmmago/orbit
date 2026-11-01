// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import Foundation

struct ReleaseAsset: Decodable, Sendable {
    let name: String
    let browserDownloadURL: URL
    let size: Int64
    let digest: String?

    enum CodingKeys: String, CodingKey {
        case name, size, digest
        case browserDownloadURL = "browser_download_url"
    }

    /// Only download uploaded assets from this project's GitHub releases.
    var isTrustedDownload: Bool {
        browserDownloadURL.scheme == "https"
            && browserDownloadURL.host == "github.com"
            && browserDownloadURL.user == nil
            && browserDownloadURL.password == nil
            && browserDownloadURL.port == nil
            && browserDownloadURL.path.hasPrefix("/mmmago/orbit/releases/download/")
            && size > 0
    }
}
