"""Public UI/SAF lifecycle driver for the installed MonsterEngine debug APK.

Uses real Android shell/UIAutomator, files and exported diagnostic protocol.
No JNI substitution or production modification. Every command has a bounded
timeout; output is durable under the current pipeline evidence directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[4]
LOGS = Path(os.environ.get("MONSTER_TEST_OUTPUT", ROOT / "out/mobile-verification/emulator"))
ADB = os.environ.get("ADB", shutil.which("adb") or "adb")
APP = "com.monsterengine.viewer.debug"


class Emulator:
    def __init__(self, serial, label):
        self.serial, self.label = serial, label
        LOGS.mkdir(parents=True, exist_ok=True)
        self.log = (LOGS / (label + "-commands.log")).open("ab")
        self.step = 0

    def command(self, *args, timeout=15, check=True):
        cmd = [ADB, "-s", self.serial, *map(str, args)]
        self.log.write((repr(cmd) + "\n").encode()); self.log.flush()
        result = subprocess.run(cmd, capture_output=True, timeout=timeout)
        self.log.write(result.stdout + result.stderr + b"\n"); self.log.flush()
        if check and result.returncode:
            raise AssertionError(f"adb exit {result.returncode}: {args}")
        return result.stdout

    def ui(self, suffix=""):
        self.command("shell", "uiautomator", "dump", "/sdcard/monster-verification-ui.xml", timeout=20)
        data = self.command("shell", "cat", "/sdcard/monster-verification-ui.xml")
        self.step += 1
        (LOGS / f"{self.label}-ui-{self.step}{suffix}.xml").write_bytes(data)
        return ET.fromstring(data)

    def click(self, text, contains=False, required=True):
        root = self.ui()
        for node in root.iter("node"):
            actual = node.get("text", "")
            if (text in actual if contains else actual == text) and node.get("enabled") == "true":
                bounds = list(map(int, re.findall(r"\d+", node.get("bounds", ""))))
                assert len(bounds) == 4
                self.command("shell", "input", "tap", (bounds[0]+bounds[2])//2, (bounds[1]+bounds[3])//2)
                return True
        if required:
            raise AssertionError("Visible enabled UI control absent: " + text)
        return False

    def back(self):
        self.command("shell", "input", "keyevent", "KEYCODE_BACK")

    def screenshot(self, name):
        path = LOGS / (self.label + "-" + name + ".png")
        path.write_bytes(self.command("exec-out", "screencap", "-p"))
        return path

    def exports(self):
        output = self.command("shell", "run-as", APP, "ls", "-t", "files/exports", check=False).decode()
        return [name.strip() for name in output.splitlines() if name.endswith(".zip")]

    def report(self, screenshot=False):
        before = set(self.exports())
        self.click("报告")
        self.click("截取三维画面并生成报告" if screenshot else "生成诊断报告")
        limit = time.monotonic() + 20
        while time.monotonic() < limit:
            exports = [name for name in self.exports() if name not in before]
            if exports:
                name = exports[0]
                data = self.command("exec-out", "run-as", APP, "cat", "files/exports/"+name)
                target = LOGS / (self.label + ("-screenshot.zip" if screenshot else "-diagnostics.zip"))
                target.write_bytes(data)
                try:
                    with zipfile.ZipFile(target) as archive:
                        report = json.loads(archive.read("report.json"))
                        (LOGS / (self.label + "-report.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
                        for member in archive.namelist():
                            if member.endswith(".png") or member.endswith(".csv") or "metadata" in member:
                                (LOGS / (self.label + "-" + Path(member).name)).write_bytes(archive.read(member))
                    if self.click("返回", required=False):
                        pass
                    return report
                except zipfile.BadZipFile:
                    time.sleep(.25)
            time.sleep(.5)
        raise AssertionError("No completed export within20s")

    def lifecycle(self, repetitions):
        evidence=[]
        self.click("画面")
        for index in range(repetitions):
            self.command("shell", "input", "keyevent", "KEYCODE_HOME")
            time.sleep(.4)
            self.command("shell", "am", "start", "-n", APP+"/com.monsterengine.viewer.MainActivity")
            time.sleep(.6)
            pid = self.command("shell", "pidof", APP).decode().strip()
            assert pid, "app process disappeared"
            root=self.ui()
            texts=[node.get("text", "") for node in root.iter("node")]
            assert "画面" in texts and "正在释放旧画面，请稍候" not in texts, "surface recovery not acknowledged"
            evidence.append({"iteration":index+1,"pid":pid,"texts":texts})
        (LOGS/(self.label+"-lifecycle.json")).write_text(json.dumps(evidence,indent=2,ensure_ascii=False),encoding="utf-8")
        self.screenshot("after-lifecycle")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=("report","screenshot","lifecycle","tap","screen","ui"))
    parser.add_argument("--serial", required=True, help="Explicit adb device serial")
    parser.add_argument("--label",required=True)
    parser.add_argument("--text")
    parser.add_argument("--repetitions",type=int,default=20)
    args=parser.parse_args();driver=Emulator(args.serial,args.label)
    if args.action=="report":driver.report()
    elif args.action=="screenshot":driver.report(True)
    elif args.action=="lifecycle":driver.lifecycle(args.repetitions)
    elif args.action=="tap":driver.click(args.text)
    elif args.action=="screen":driver.screenshot("screen")
    elif args.action=="ui":driver.ui()


if __name__=="__main__":main()
