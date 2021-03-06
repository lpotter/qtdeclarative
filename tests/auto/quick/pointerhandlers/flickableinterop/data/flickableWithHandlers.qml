/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

import QtQuick 2.8
import Qt.labs.handlers 1.0

Rectangle {
    id: root
    width: 500
    height: 480
    objectName: "root"
    color: "#222222"

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        anchors.topMargin: 40
        contentHeight: 600
        contentWidth: 600
//        pressDelay: TODO

        Row {
            spacing: 6
            Slider {
                label: "DragHandler"
                objectName: "Slider"
                value: 49; width: 100; height: 400
            }
            Column {
                spacing: 6
                TapHandlerButton {
                    objectName: "DragThreshold"
                    label: "DragThreshold"
                    gesturePolicy: TapHandler.DragThreshold
                }
                TapHandlerButton {
                    objectName: "WithinBounds"
                    label: "WithinBounds"
                    gesturePolicy: TapHandler.WithinBounds
                }
                TapHandlerButton {
                    objectName: "ReleaseWithinBounds"
                    label: "ReleaseWithinBounds"
                    gesturePolicy: TapHandler.ReleaseWithinBounds // the default
                }
            }
            Column {
                spacing: 6
                Rectangle {
                    width: 50
                    height: 50
                    color: "aqua"
                    border.color: drag1.active ? "darkgreen" : "transparent"
                    border.width: 3
                    objectName: "drag"
                    DragHandler {
                        id: drag1
                    }
                    Text {
                        anchors.centerIn: parent
                        enabled: false
                        text: "drag"
                    }
                }
                Rectangle {
                    width: 50
                    height: 50
                    color: "aqua"
                    objectName: "tap"
                    border.color: tap1.isPressed ? "red" : "transparent"
                    border.width: 3
                    TapHandler {
                        id: tap1
                        gesturePolicy: TapHandler.DragThreshold
                    }
                    Text {
                        anchors.centerIn: parent
                        enabled: false
                        text: "tap"
                    }
                }
                Rectangle {
                    width: 50
                    height: 50
                    color: "aqua"
                    border.color: tap2.isPressed ? "red" : drag2.active ? "darkgreen" : "transparent"
                    border.width: 3
                    objectName: "dragAndTap"
                    DragHandler {
                        id: drag2
                    }
                    TapHandler {
                        id: tap2
                        gesturePolicy: TapHandler.DragThreshold
                    }
                    Text {
                        anchors.centerIn: parent
                        enabled: false
                        text: "drag\nand\ntap"
                    }
                }
                Rectangle {
                    width: 50
                    height: 50
                    color: "aqua"
                    border.color: tap3.isPressed ? "red" : drag3.active ? "darkgreen" : "transparent"
                    border.width: 3
                    objectName: "tapAndDrag"
                    TapHandler {
                        id: tap3
                        gesturePolicy: TapHandler.DragThreshold
                    }
                    DragHandler {
                        id: drag3
                    }
                    Text {
                        anchors.centerIn: parent
                        enabled: false
                        text: "tap\nand\ndrag"
                    }
                }
            }
        }
    }
}

