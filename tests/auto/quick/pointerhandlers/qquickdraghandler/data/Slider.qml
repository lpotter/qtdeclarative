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

Item {
    id: root
    objectName: label
    property int value: 50
    property int maximumValue: 99
    property alias label: label.text
    property alias tapEnabled: tap.enabled
    property alias pressed: tap.pressed
    signal tapped
    width: 140
    height: 400

    Rectangle {
        id: slot
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 10
        anchors.topMargin: 30
        anchors.bottomMargin: 30
        anchors.horizontalCenter: parent.horizontalCenter
        width: 10
        color: "black"
        radius: width / 2
        smooth: true
    }

    Rectangle {
        id: glow
        anchors.fill: knob
        anchors.margins: -5
        anchors.leftMargin: -2
        anchors.horizontalCenterOffset: 1
        radius: 5
        color: "#4400FFFF"
        opacity: tap.pressed || tapFlash.running ? 1 : 0
        FlashAnimation on visible {
            id: tapFlash
        }
    }
    Rectangle {
        id: knob
        objectName: root.label + " Knob"
        width: parent.width - 2
        height: 30
        radius: 5
        color: "darkgray"
        border.color: "black"
        property bool programmatic: false
        property real multiplier: root.maximumValue / (dragHandler.yAxis.maximum - dragHandler.yAxis.minimum)
        onYChanged: if (!programmatic) root.value = root.maximumValue - (knob.y - dragHandler.yAxis.minimum) * multiplier
        transformOrigin: Item.Center
        function setValue(value) { knob.y = dragHandler.yAxis.maximum - value / knob.multiplier }
        function flash() { tapFlash.start() }
        DragHandler {
            id: dragHandler
            objectName: label.text + " DragHandler"
            xAxis.enabled: false
            yAxis.minimum: slot.y
            yAxis.maximum: slot.height + slot.y - knob.height
        }
        TapHandler {
            id: tap
            objectName: label.text + " TapHandler"
            gesturePolicy: TapHandler.DragThreshold
            onTapped: {
                tapFlash.start()
                root.tapped
            }
        }
    }

    Text {
        font.pointSize: 16
        color: "red"
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.value
    }

    Text {
        id: label
        font.pointSize: 12
        color: "red"
        anchors.top: parent.top
        anchors.topMargin: 5
        anchors.horizontalCenter: parent.horizontalCenter
    }

    Component.onCompleted: {
        knob.programmatic = true
        knob.setValue(root.value)
        knob.programmatic = false
    }
}
