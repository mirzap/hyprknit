import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

// Hyprknit in the bar: a ball of yarn that opens the knitting menu.
//
// Left click opens the menu, right click takes the sweaters off or puts them
// back on, and the wheel steps through the shared patterns. Everything goes
// through hyprctl, so the widget works with any build of the Hyprland plugin
// and simply reads as off when the plugin is not loaded.
BarWidget {
  id: root
  moduleName: "io.github.mirzap.hyprknit"

  // The menu ships beside this file, wherever `omarchy plugin add` cloned it.
  readonly property string menu:
    String(Qt.resolvedUrl("hyprknit-menu")).replace(/^file:\/\//, "")
  readonly property string installer:
    String(Qt.resolvedUrl("hyprknit-plugin")).replace(/^file:\/\//, "")

  property string pattern: ""
  property bool knitting: false
  property bool loaded: false

  function refresh() {
    if (!reader.running) reader.running = true
  }

  function openMenu(args) {
    if (!root.bar) return
    root.bar.run(Util.shellQuote(root.menu) + (args ? " " + args : ""))
    settle.restart()
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  Component.onCompleted: refresh()

  // One hyprctl call answers both questions: whether the plugin is loaded, and
  // what it is knitting.
  Process {
    id: reader
    command: ["bash", "-c",
      "hyprctl -j getoption plugin:hyprknit:pattern; echo; "
        + "hyprctl -j getoption plugin:hyprknit:enabled; "
        + Util.shellQuote(root.installer) + " repair >/dev/null 2>&1"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        var parts = text.split("\n").filter(function(line) { return line.trim() !== "" })
        try {
          var pattern = JSON.parse(parts[0])
          var enabled = JSON.parse(parts[1])
          root.loaded = typeof pattern.str === "string"
          root.pattern = root.loaded ? pattern.str : ""
          root.knitting = root.loaded && (enabled.int === undefined ? enabled.bool !== false : enabled.int !== 0)
        } catch (error) {
          root.loaded = false
          root.knitting = false
        }
      }
    }
  }

  // A change made from the menu lands a moment after the click.
  Timer {
    id: settle
    interval: 1500
    onTriggered: root.refresh()
  }

  // Someone may change the pattern from a terminal, too.
  Timer {
    interval: 10000
    running: true
    repeat: true
    onTriggered: root.refresh()
  }

  IpcHandler {
    target: "io.github.mirzap.hyprknit"

    function refresh(): void { root.broadcast("refresh") }
    function menu(): void { root.openMenu("") }
  }

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.setting("icon", "🧶")
    dimmed: !root.knitting
    tooltipText: !root.loaded ? "Hyprknit: click to install the knitting"
      : !root.knitting ? "Sweaters are off"
      : "Sweaters: " + root.pattern

    onPressed: function(mouseButton) {
      if (mouseButton === Qt.RightButton) root.openMenu("toggle")
      else root.openMenu("")
    }
    onWheelMoved: function(delta) {
      root.openMenu(delta > 0 ? "previous" : "next")
    }
  }
}
