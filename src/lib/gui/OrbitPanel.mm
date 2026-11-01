/*
 * Orbit -- keyboard, mouse and trackpad sharing between Macs
 * SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * Orbit file added: 2026-11-01 by Lucas Magotteaux; see NOTICE.md.
 */

#include "OrbitPanel.h"

#import <AppKit/AppKit.h>
#import "OrbitPanel-Swift.h"

#include <QJsonDocument>
#include <QPointer>

struct OrbitPanel::Impl
{
  OrbitPanelController *controller = nil; // ARC keeps it alive
};

OrbitPanel::OrbitPanel(QObject *parent) : QObject(parent), m_impl(std::make_unique<Impl>())
{
  m_impl->controller = [[OrbitPanelController alloc] init];
  QPointer<OrbitPanel> self = this;
  m_impl->controller.onAction = ^(NSString *name, NSString *arguments) {
    if (!self)
      return;
    const auto json = QJsonDocument::fromJson(QByteArray::fromNSData([arguments dataUsingEncoding:NSUTF8StringEncoding]));
    Q_EMIT self->actionRequested(QString::fromNSString(name), json.object());
  };
}

OrbitPanel::~OrbitPanel()
{
  m_impl->controller.onAction = nil;
  [m_impl->controller hide];
}

void OrbitPanel::setState(const QJsonObject &state)
{
  const auto json = QJsonDocument(state).toJson(QJsonDocument::Compact);
  [m_impl->controller update:json.toNSData() ? [[NSString alloc] initWithData:json.toNSData() encoding:NSUTF8StringEncoding] : @"{}"];
}

void OrbitPanel::installStatusItem()
{
  [m_impl->controller installStatusItem];
}

void OrbitPanel::toggle()
{
  [m_impl->controller toggle];
}

void OrbitPanel::show()
{
  [m_impl->controller show];
}

void OrbitPanel::hide()
{
  [m_impl->controller hide];
}
