// SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
// SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

import Foundation

struct GitHubRelease: Decodable, Sendable {
    let tagName: String
    let draft: Bool
    let prerelease: Bool
    let assets: [ReleaseAsset]

    enum CodingKeys: String, CodingKey {
        case draft, prerelease, assets
        case tagName = "tag_name"
    }

    func asset(for architecture: String) -> ReleaseAsset? {
        let names = ["Orbit-macos-\(architecture).zip", "Orbit-macos-universal.zip", "Orbit.zip"]
        for name in names {
            if let asset = assets.first(where: { $0.name == name && $0.isTrustedDownload }) {
                return asset
            }
        }
        return nil
    }

    static var architecture: String {
        #if arch(arm64)
        "arm64"
        #else
        "x86_64"
        #endif
    }
}
