/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mhome.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/
#include <QX11Info>
#include <QApplication>
#include <MLayout>
#include <MFlowLayoutPolicy>

#include "switcher.h"
#include "switcherbutton.h"
#include "windowinfo.h"
#include "mainwindow.h"
#include "x11wrapper.h"

// The time to wait until updating the model when a new application is started
#define UPDATE_DELAY_MS 700

Switcher::Switcher(MWidget *parent) :
    MWidgetController(new SwitcherModel, parent), windowListUpdated(false)
{
    // Connect to the windowListUpdated signal of the HomeApplication to get information about window list changes
    connect(qApp, SIGNAL(windowListUpdated(const QList<WindowInfo> &)), this, SLOT(updateWindowList(const QList<WindowInfo> &)));
    connect(qApp, SIGNAL(windowTitleChanged(Window, QString)), this, SLOT(changeWindowTitle(Window, QString)));

    // Get the X11 Atoms for closing and activating a window
    closeWindowAtom  = X11Wrapper::XInternAtom(QX11Info::display(), "_NET_CLOSE_WINDOW",  False);
    activeWindowAtom = X11Wrapper::XInternAtom(QX11Info::display(), "_NET_ACTIVE_WINDOW", False);
}

Switcher::~Switcher()
{
}

void Switcher::updateWindowList(const QList<WindowInfo> &newList)
{
    int previousWindowCount = windowList.count();
    windowList = newList;

    windowListUpdated = true;

    // If no new windows have appeared, i.e. an application is not currently
    // starting, update the buttons immediately - otherwise, wait a bit
    if( previousWindowCount >= newList.count() ) {
        updateButtons();
    } else {
        QTimer::singleShot(UPDATE_DELAY_MS, this, SLOT(updateButtons()));
    }
}

void Switcher::windowToFront(Window window)
{
    XEvent ev;
    memset(&ev, 0, sizeof(ev));

    Display *display = QX11Info::display();

    Window rootWin = QX11Info::appRootWindow(QX11Info::appScreen());

    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = window;
    ev.xclient.message_type = activeWindowAtom;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = 1;
    ev.xclient.data.l[1]    = CurrentTime;
    ev.xclient.data.l[2]    = 0;

    X11Wrapper::XSendEvent(display,
                           rootWin, False,
                           StructureNotifyMask, &ev);
}

void Switcher::closeWindow(Window window)
{
    XEvent ev;
    memset(&ev, 0, sizeof(ev));

    Display *display = QX11Info::display();

    Window rootWin = QX11Info::appRootWindow(QX11Info::appScreen());

    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = window;
    ev.xclient.message_type = closeWindowAtom;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = CurrentTime;
    ev.xclient.data.l[1]    = rootWin;

    X11Wrapper::XSendEvent(display,
                           rootWin, False,
                           SubstructureRedirectMask, &ev);
}

void Switcher::viewportSizePosChanged(const QSizeF &, const QRectF &, const QPointF &)
{
    foreach(QSharedPointer<SwitcherButton> b, model()->buttons()) {
        // Update the icon geometry of each button when the viewport is panned
        b->updateIconGeometry();
    }
}

void Switcher::changeWindowTitle(Window window,  const QString &title)
{
    if (windowMap.contains(window)) {
        windowMap.value(window)->setText(title);
        windowMap.value(window)->update();
    }
}


void Switcher::updateButtons()
{
    if( windowListUpdated ) {

        windowListUpdated = false;

        QList< QSharedPointer<SwitcherButton> > oldButtons(model()->buttons());

        // List of existing buttons for which a window still exists
        QList< QSharedPointer<SwitcherButton> > currentButtons;

        // List of newly created buttons
        QList< QSharedPointer<SwitcherButton> > newButtons;

        // List to be set as the new list in the model
        QList< QSharedPointer<SwitcherButton> > nextButtons;

        // The new mapping of known windows to the buttons
        QMap<Window, SwitcherButton *> newWindowMap;

        // Go through the windows and create new buttons for new windows
        foreach(WindowInfo wi, windowList) {
            if (windowMap.contains(wi.window())) {
                SwitcherButton *b = windowMap[wi.window()];

                // Button already exists - set title (as it may have changed)
                b->setText(wi.title());

                newWindowMap[wi.window()] = b;
            } else {
                QSharedPointer<SwitcherButton> b(new SwitcherButton(wi.title(), NULL, wi.window()));
                connect(b.data(), SIGNAL(windowToFront(Window)), this, SLOT(windowToFront(Window)));
                connect(b.data(), SIGNAL(closeWindow(Window)), this, SLOT(closeWindow(Window)));

                newButtons.append(b);

                newWindowMap[wi.window()] = b.data();
            }
        }

        foreach(QSharedPointer<SwitcherButton> b, oldButtons) {
            // Keep only the buttons for which a window still exists
            if (newWindowMap.contains(b.data()->xWindow())) {
                currentButtons.append(b);
            }
        }

        windowMap = newWindowMap;
        nextButtons.append(currentButtons);
        nextButtons.append(newButtons);

        // Take the new set of buttons into use
        model()->setButtons(nextButtons);
    }
}

void Switcher::scheduleUpdate() {
    QTimer::singleShot(UPDATE_DELAY_MS, this, SLOT(updateButtons()));
}
