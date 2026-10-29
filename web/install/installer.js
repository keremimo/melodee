// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Portions Copyright (C) 2026 Charles Vestal
// Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// The installer page (index.html): find the FM-1, show what it runs, choose m-bootloader, then install in a
// screen of its own. Before writing: the rules (stay on the tab, keep the cable and power) and the backup. While
// writing: keepalive.js holds the page (one install at a time, awake, asks before leaving) and the screen says so
// again whenever the tab comes back. An install that stops after the FM-1 entered update mode can be resumed,
// now or on a later visit (a mark in localStorage and the device itself say so). fm1-base's installer.html is the
// origin of the m-bootloader option, the stock firmware path and the crash report.

import { productOf, logicalImage } from "../fm1pkg.js";
import { SteadyUpdater } from "./transfer.js";
import { hold, busyElsewhere } from "./keepalive.js";
import { describeDevice, planFor, progressModel, remaining, afterFailure } from "./progress.js";
import { MELODEE_LOADER, STOCK_LOADER } from "../fm1ota.js";
import { BackupConnection } from "../fm1backup.js";
import { installResources, resHeader, resStatus, RES_COMMAND } from "../fm1res.js";
import { setupLeaving } from "./leave.js";

const IS_OTA = (id) => /^ota-/i.test(id.model);
const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const DEMO = params.has("demo") ? (params.get("demo") || "stock") : null;
const MARK = "melodee-install-pending";               // an install that has not reported Done (localStorage)
const LANG = "melodee-lang";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const IS_STOCK = (p) => /^FM-1_0\d\d$/.test(p) && p !== "FM-1_099";
const fail = (code, msg, detail = "") => Object.assign(new Error(msg), { code, detail });

const TEXT = {
  en: {
    navEditor: "Editor", navInstall: "Install", navManual: "Manual", other: "日本語",
    demo: "Demo: a simulated FM-1. Nothing is written.", demoleave: "Leave the demo",
    kicker: "Firmware for the M-VAVE FM-1", title: "Install Melodee",
    lede: "Synths, sequencing and effects for the FM-1, installed over USB from this page.",
    factBrowser: "Chrome or Edge", factCable: "A USB data cable", factTime: "About 3 minutes",
    resumeBannerH: "An installation didn't finish.",
    resumeBanner: "Your FM-1 is waiting in update mode. Connect it and press Find FM-1 to finish.",
    s1h: "Connect the FM-1",
    s1p: "Plug it straight into this computer, without a hub, and switch it on. Close other music apps and editor tabs. When the browser asks to use MIDI devices, choose Allow.",
    find: "Find FM-1", findAgain: "Look again", finding: "Looking for the FM-1…",
    s2h: "Recovery bootloader", optional: "Optional",
    mboot: "Also install m-bootloader {v}",
    mbootwhy: "A recovery menu (hold PLAY while switching on) and USB download mode (hold REC) that work even when a firmware won't start. It replaces M-VAVE's bootloader and stays through firmware updates.",
    mbootrisk: "It writes the FM-1's boot area, the one step where a power cut can need hardware recovery. Leave it off if you're unsure: you can add it later from this page.",
    mbootby: "By Julius Vitkauskas, MIT licence.", newtab: " (opens in a new tab)",
    s3h: "Install", s3p: "Back up your sounds first: Settings, then Backup, in the editor.",
    gohint: "Find your FM-1 first.",
    goInstall: "Install Melodee {v}", goUpdate: "Update to Melodee {v}", goReinstall: "Reinstall Melodee {v}",
    goResume: "Resume installation", goBoot: "Install m-bootloader",
    planInstall: "Melodee {v} replaces this FM-1's firmware. Back up your sounds first: Settings, then Backup, in the editor.",
    planUpdate: "Back up your sounds before updating: Settings, then Backup, in the editor.",
    planResume: "The FM-1 is waiting in update mode. Resuming writes Melodee {v} again from the start of the write.",
    planThis: "Melodee {v} is already on this FM-1. Reinstalling is safe and keeps your sounds.",
    planBoot: "Melodee {v} is already on this FM-1, so only m-bootloader is written.",
    planOpen: "This FM-1 runs M-VAVE's Open firmware. Go back to M-VAVE's firmware first (below), then install Melodee.",
    planModel: "This is not an FM-1 ({id}). Melodee runs only on the M-VAVE FM-1.",
    devLoader: "In update mode: an installation is waiting to finish", devThis: "Melodee {v}", devMelodee: "Melodee {v}",
    devDev: "FM-1 development firmware (FM-1_900)", devStock: "M-VAVE firmware {v}", devOpen: "M-VAVE Open firmware",
    devOther: "Another firmware ({id})", devModel: "Not an FM-1 ({id})",
    chipReady: "Ready", chipResume: "Update mode", chipCurrent: "Up to date", chipBlocked: "Can't install",
    chipSetup: "Setup unfinished", goSetup: "Finish setup",
    planSetup: "Melodee {v} is on this FM-1, but its fonts and factory sounds aren't. Finish setup writes only them and keeps your sounds.",
    nomidi: "This browser can't reach USB MIDI devices. Open this page in Chrome or Edge on a computer.",
    denied: "MIDI access was not allowed. Allow MIDI devices in the site settings (the icon in the address bar), then press Find FM-1 again.",
    notfound: "No FM-1 answered. Check that it is on and connected with a data cable (not a charging-only one), close other apps and tabs that use MIDI, then look again.",
    busy: "An installation is already running in another tab. Finish it there first.",
    pending: "Select the same version and build as the interrupted installation before resuming.",
    loadfail: "The firmware package could not be loaded: {m}", badpkg: "The firmware package is damaged. Reload the page.",
    nopkg: "There is no firmware package beside this page. Try the demo instead:",
    pfh: "Before you start",
    pf1h: "Keep this tab open and in front", pf1: "Don't switch tabs, minimise the window or close it until the page says Done.",
    pf2h: "Leave the FM-1 connected and on", pf2: "Don't unplug the cable or switch the FM-1 off. Plug a laptop into power.",
    pf3h: "It takes about 3 minutes", pf3: "The FM-1's screen shows UPDATING, or stays dark. Both are normal.",
    pfboot: "m-bootloader is written last, after one more restart. Keep the power on until the page says Done.",
    pfbootonly: "Writing the boot area takes a few seconds. Keep the power on until the page says Done.",
    pfok: "I've backed up my sounds, or have nothing to keep.", cancel: "Cancel", start: "Start installation",
    runwarn: "Keep this tab open and in front. Don't unplug the FM-1.",
    runInstall: "Installing Melodee {v}", runResume: "Finishing the installation", runBoot: "Installing m-bootloader",
    runStock: "Installing M-VAVE's firmware", runSetup: "Finishing setup",
    phPrepare: "Prepare", phWrite: "Write", phRestart: "Restart", phSetup: "Setup", phBoot: "m-bootloader",
    stStart: "Starting", stVerify: "Checking the package", stLoader: "Switching the FM-1 to update mode",
    stWrite: "Writing the firmware", stReboot: "Restarting the FM-1", stBootWait: "Waiting for Melodee to start",
    stBoot: "Writing m-bootloader. The FM-1 restarts by itself", stFind: "Finding the FM-1 in update mode",
    stRes: "Writing the fonts and factory sounds",
    etaMin: "About {n} min left", etaSec: "Less than a minute left",
    stall: "Waiting for the FM-1… keep the cable connected.",
    backKept: "You left this tab for {s}. The installation kept going, but please stay here until it's done.",
    backSlow: "You left this tab for {s}, and the browser slowed the page. It continues now: please stay on this tab.",
    titleRun: "{p}% · Installing Melodee", titleHidden: "⚠ Come back: installing, {p}%", titleDone: "Done · Melodee",
    leave: "Installing. Leaving now can stop the installation.",
    endDoneH: "Melodee {v} is installed", endDoneBootH: "Melodee {v} and m-bootloader are installed",
    endDone: "The FM-1 has restarted with Melodee {v}. You can unplug it now.",
    endBootH: "m-bootloader is installed", endBoot: "The FM-1 has restarted with Melodee {v}.",
    endStockH: "M-VAVE's firmware is back", endStock: "The FM-1 has restarted with its original firmware.",
    endSetupDoneH: "Setup is finished", endSetupDone: "The fonts and factory sounds are on the FM-1. You can unplug it now.",
    endSetupH: "Setup isn't finished",
    endSetup: "Melodee {v} runs, but its fonts and factory sounds weren't written. Until they are, the FM-1 uses a plain font and INIT sounds.",
    tipResOk: "The fonts and factory sounds are installed.",
    tipResRestart: "Switch the FM-1 off and on once, so every sound list shows the factory sounds.",
    tipEditor: "Open the editor to browse sounds and make backups.",
    tipBoot: "To check m-bootloader: switch the FM-1 off, hold PLAY and switch it on. Its recovery menu appears.",
    endPausedH: "Installation paused",
    endPaused: "The FM-1 is safe. It waits in update mode until the installation finishes, even if it's switched off.",
    tipCable: "Check the cable, or plug it in again. The screen shows UPDATING, or stays dark.",
    tipApps: "Close other music apps and editor tabs.", tipResume: "Then press Resume. The write starts again from the beginning.",
    endSafeH: "Nothing was changed", endSafe: "The FM-1 still runs its previous firmware.",
    endRestartH: "The FM-1 didn't come back",
    endRestart: "The write finished, but the FM-1 didn't report back. Switch it off and on again.",
    tipStill: "If it shows UPDATING or stays dark, it is still in update mode: press Find FM-1 and resume.",
    tipBrick: "If it doesn't start at all, see “If the FM-1 won't start” on this page.",
    endPartialH: "Melodee is installed, m-bootloader isn't",
    endPartial: "Melodee {v} is installed. m-bootloader could not be written and nothing else was changed. You can try again later from this page.",
    btnEditor: "Open the editor", btnManual: "Read the manual", btnClose: "Close", btnResume: "Resume installation",
    btnRetry: "Try again", btnSetup: "Finish setup",
    e_resfetch: "They could not be downloaded: {d}", e_resfile: "The downloaded file is damaged. Reload the page, then press Finish setup.",
    e_resnoanswer: "The FM-1 didn't answer after restarting. Check the cable, then press Finish setup.",
    e_reslost: "The FM-1 stopped answering. Keep the cable connected and press Finish setup.",
    e_resnocmd: "The firmware on the FM-1 can't take them. Install this version first.",
    e_resbuild: "The FM-1 runs another build than the one selected here. Install this version first.",
    e_resbusy: "The FM-1 is playing, or its Bluetooth is on. Stop playback and switch Bluetooth off, then press Finish setup.",
    e_resroom: "The FM-1 couldn't make room for them. If its storage is full, delete a project you don't need, then press Finish setup.",
    e_reswrite: "Writing stopped part way. Keep the cable connected and press Finish setup.",
    e_rescheck: "What was written didn't check out. Press Finish setup to write it again.",
    e_notfound: "The FM-1 stopped answering before anything was written.",
    e_model: "The connected device is not an FM-1.",
    e_lost: "The USB connection was lost.", e_stopped: "The FM-1 stopped asking for data.",
    e_noloader: "The FM-1 in update mode was not found.", e_noreturn: "The FM-1 did not come back after restarting.",
    e_mismatch: "Written, but the FM-1 reports another version: {d}.", e_badreq: "The FM-1 asked for data outside the package.",
    e_open: "This FM-1 runs M-VAVE's Open firmware. Go back to M-VAVE's firmware first.",
    e_bootrefused: "The firmware refused m-bootloader.", e_other: "Error: {m}",
    mResumeH: "If an installation stopped",
    mResume1: "Nothing is lost. Until an installation finishes, the FM-1 starts in update mode: its screen shows UPDATING, or stays dark. That holds even after switching it off.",
    mResume2: "Connect it, open this page and press Find FM-1. The page offers to resume, and continues from the start of the write.",
    mStockH: "Go back to M-VAVE's firmware",
    mStock1: "This writes M-VAVE's own FM-1 firmware back over the same cable. It also puts back M-VAVE's bootloader, and what Melodee saved may be lost.",
    mStock2: "Download M-VAVE's firmware:", mStock3: "Choose the file you downloaded:",
    mStock4: "Connect the FM-1, press Find FM-1 above, then the button below.", mStockGo: "Install M-VAVE's firmware",
    stockok: "That is M-VAVE's FM-1 firmware ({p}).", stockbad: "That is not M-VAVE's FM-1 firmware. Choose the file from the link above.",
    stockfind: "Find the FM-1 above first.",
    mReportH: "Report a problem",
    mReport1: "If the FM-1 crashed, don't switch it off: the crash record survives a restart, not a power cycle. Connect it and read the record, then send it with what you were doing.",
    mReportGo: "Read the crash report", mReportCopy: "Copy", mReportLabel: "Crash report",
    reportwait: "Reading…", reportnone: "No answer. Check that the FM-1 is running Melodee.",
    reportok: "Read. Copy it and send it.", reportcopied: "Copied.",
    mBrickH: "If the FM-1 won't start",
    mBrick1: "With m-bootloader: switch off, hold PLAY and switch on for its recovery menu, or hold REC for USB download mode.",
    mBrick2: "Without it, the FM-1's USB recovery route can put a firmware back from a computer:",
    mBrickLink: "felucca-bsp recovery tools",
    credits: "Melodee by Kerem Kilic (Ellic Studio). Built on fm1-base by Charles Vestal and felucca-bsp, from Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments. m-bootloader by Julius Vitkauskas.",
    legal: "Firmware: MPL-2.0. Editor: GPL-3.0-only. Third-party terms apply. Without warranty. M-VAVE and FM-1 are trademarks of their respective owners.",
    details: "Details", sec: "{n} s", min: "{n} min",
  },
  ja: {
    navEditor: "エディター", navInstall: "インストール", navManual: "マニュアル", other: "English",
    demo: "デモ：FM-1 をシミュレートしています。何も書き込まれません。", demoleave: "デモを終了",
    kicker: "M-VAVE FM-1 用ファームウェア", title: "Melodee をインストール",
    lede: "FM-1 用シンセサイザー、シーケンサー、エフェクト。このページから USB でインストールできます。",
    factBrowser: "Chrome または Edge", factCable: "USB データケーブル", factTime: "約 3 分",
    resumeBannerH: "前回のインストールが完了していません。",
    resumeBanner: "FM-1 はアップデートモードで待機しています。接続して「FM-1 を探す」を押すと、続きから完了できます。",
    s1h: "FM-1 を接続する",
    s1p: "ハブを使わずにこのパソコンへ直接つなぎ、電源を入れてください。ほかの音楽アプリやエディターのタブは閉じてください。ブラウザが MIDI デバイスの使用許可を求めたら「許可」を選びます。",
    find: "FM-1 を探す", findAgain: "もう一度探す", finding: "FM-1 を探しています…",
    s2h: "リカバリー用ブートローダー", optional: "任意",
    mboot: "m-bootloader {v} もインストールする",
    mbootwhy: "ファームウェアが起動しないときでも使えるリカバリーメニュー（PLAY を押しながら電源オン）と USB ダウンロードモード（REC）を追加します。M-VAVE のブートローダーと置き換わり、ファームウェアを更新しても残ります。",
    mbootrisk: "FM-1 のブート領域に書き込みます。この工程中に電源が切れると、ハードウェアでの復旧が必要になることがあります。迷ったらオフのままで構いません。あとからこのページで追加できます。",
    mbootby: "Julius Vitkauskas 作、MIT ライセンス。", newtab: "（新しいタブで開きます）",
    s3h: "インストール", s3p: "先にサウンドをバックアップしてください（エディターの「設定」→「バックアップ」）。",
    gohint: "先に FM-1 を探してください。",
    goInstall: "Melodee {v} をインストール", goUpdate: "Melodee {v} にアップデート", goReinstall: "Melodee {v} を再インストール",
    goResume: "インストールを再開", goBoot: "m-bootloader をインストール",
    planInstall: "この FM-1 のファームウェアを Melodee {v} に置き換えます。先にサウンドをバックアップしてください（エディターの「設定」→「バックアップ」）。",
    planUpdate: "アップデート前にサウンドをバックアップしてください（エディターの「設定」→「バックアップ」）。",
    planResume: "FM-1 はアップデートモードで待機しています。再開すると、書き込みの最初から Melodee {v} を書き込み直します。",
    planThis: "この FM-1 には Melodee {v} がすでに入っています。再インストールしても安全で、サウンドは残ります。",
    planBoot: "この FM-1 には Melodee {v} がすでに入っているため、m-bootloader だけを書き込みます。",
    planOpen: "この FM-1 は M-VAVE の Open ファームウェアで動いています。先に下の手順で M-VAVE のファームウェアに戻してから、Melodee をインストールしてください。",
    planModel: "これは FM-1 ではありません（{id}）。Melodee は M-VAVE FM-1 専用です。",
    devLoader: "アップデートモード：インストールの完了待ち", devThis: "Melodee {v}", devMelodee: "Melodee {v}",
    devDev: "FM-1 開発版（FM-1_900）", devStock: "M-VAVE ファームウェア {v}", devOpen: "M-VAVE Open ファームウェア",
    devOther: "別のファームウェア（{id}）", devModel: "FM-1 ではありません（{id}）",
    chipReady: "準備完了", chipResume: "アップデートモード", chipCurrent: "最新", chipBlocked: "インストール不可",
    chipSetup: "セットアップ未完了", goSetup: "セットアップを完了",
    planSetup: "この FM-1 には Melodee {v} が入っていますが、フォントとファクトリーサウンドがまだです。「セットアップを完了」はそれだけを書き込み、サウンドは残ります。",
    nomidi: "このブラウザは USB MIDI デバイスを使えません。パソコンの Chrome か Edge で開いてください。",
    denied: "MIDI の使用が許可されませんでした。アドレスバーのアイコンからサイトの設定で MIDI デバイスを許可し、もう一度「FM-1 を探す」を押してください。",
    notfound: "FM-1 が応答しません。電源が入っていて、データ通信用のケーブル（充電専用ではないもの）でつながっているか確かめ、MIDI を使うほかのアプリやタブを閉じてから、もう一度探してください。",
    pending: "中断したインストールと同じバージョンとビルドを選んでから再開してください。",
    busy: "別のタブでインストールが進行中です。先にそちらを完了させてください。",
    loadfail: "ファームウェアを読み込めませんでした：{m}", badpkg: "ファームウェアのファイルが壊れています。ページを再読み込みしてください。",
    nopkg: "このページの隣にファームウェアがありません。代わりにデモを試せます：",
    pfh: "始める前に",
    pf1h: "このタブを開いたまま、前面に", pf1: "「完了」と表示されるまで、タブの切り替え、ウィンドウの最小化、ページを閉じることはしないでください。",
    pf2h: "FM-1 は接続したまま、電源も入れたまま", pf2: "ケーブルを抜いたり、FM-1 の電源を切ったりしないでください。ノートパソコンは電源につないでください。",
    pf3h: "約 3 分かかります", pf3: "FM-1 の画面には UPDATING と表示されるか、暗いままになります。どちらも正常です。",
    pfboot: "m-bootloader は最後に、もう一度再起動してから書き込みます。「完了」と表示されるまで電源を切らないでください。",
    pfbootonly: "ブート領域の書き込みは数秒で終わります。「完了」と表示されるまで電源を切らないでください。",
    pfok: "サウンドをバックアップした（または残したいものはない）", cancel: "キャンセル", start: "インストールを開始",
    runwarn: "このタブを開いたまま前面に。FM-1 のケーブルを抜かないでください。",
    runInstall: "Melodee {v} をインストール中", runResume: "インストールを完了しています", runBoot: "m-bootloader をインストール中",
    runStock: "M-VAVE のファームウェアをインストール中", runSetup: "セットアップを完了しています",
    phPrepare: "準備", phWrite: "書き込み", phRestart: "再起動", phSetup: "セットアップ", phBoot: "m-bootloader",
    stStart: "開始しています", stVerify: "ファイルを確認しています", stLoader: "FM-1 をアップデートモードに切り替えています",
    stWrite: "ファームウェアを書き込んでいます", stReboot: "FM-1 を再起動しています", stBootWait: "Melodee の起動を待っています",
    stBoot: "m-bootloader を書き込んでいます。FM-1 は自動で再起動します", stFind: "アップデートモードの FM-1 を探しています",
    stRes: "フォントとファクトリーサウンドを書き込んでいます",
    etaMin: "残り約 {n} 分", etaSec: "残り 1 分未満",
    stall: "FM-1 を待っています… ケーブルは接続したままにしてください。",
    backKept: "このタブを {s} 離れていました。インストールは続いていましたが、完了までこのタブにいてください。",
    backSlow: "このタブを {s} 離れていたため、ブラウザがページを遅くしていました。今は再開しています。このタブにいてください。",
    titleRun: "{p}% · Melodee をインストール中", titleHidden: "⚠ 戻ってください：インストール中 {p}%", titleDone: "完了 · Melodee",
    leave: "インストール中です。今ページを離れると中断することがあります。",
    endDoneH: "Melodee {v} をインストールしました", endDoneBootH: "Melodee {v} と m-bootloader をインストールしました",
    endDone: "FM-1 は Melodee {v} で再起動しました。ケーブルを抜いて構いません。",
    endBootH: "m-bootloader をインストールしました", endBoot: "FM-1 は Melodee {v} で再起動しました。",
    endStockH: "M-VAVE のファームウェアに戻しました", endStock: "FM-1 は純正ファームウェアで再起動しました。",
    endSetupDoneH: "セットアップが完了しました", endSetupDone: "フォントとファクトリーサウンドを FM-1 に書き込みました。ケーブルを抜いて構いません。",
    endSetupH: "セットアップが完了していません",
    endSetup: "Melodee {v} は動いていますが、フォントとファクトリーサウンドを書き込めませんでした。書き込むまでは、FM-1 はシンプルなフォントと INIT サウンドを使います。",
    tipResOk: "フォントとファクトリーサウンドをインストールしました。",
    tipResRestart: "FM-1 の電源を一度切って入れ直すと、すべてのサウンドリストにファクトリーサウンドが表示されます。",
    tipEditor: "エディターでサウンドの閲覧やバックアップができます。",
    tipBoot: "m-bootloader の確認：FM-1 の電源を切り、PLAY を押しながら電源を入れると、リカバリーメニューが表示されます。",
    endPausedH: "インストールが中断しました",
    endPaused: "FM-1 は安全です。インストールが完了するまで、電源を切ってもアップデートモードで待機します。",
    tipCable: "ケーブルを確かめるか、挿し直してください。画面には UPDATING と表示されるか、暗いままです。",
    tipApps: "ほかの音楽アプリやエディターのタブを閉じてください。", tipResume: "それから「再開」を押します。書き込みを最初からやり直します。",
    endSafeH: "何も変更されていません", endSafe: "FM-1 は以前のファームウェアのままです。",
    endRestartH: "FM-1 が戻ってきません",
    endRestart: "書き込みは終わりましたが、FM-1 から応答がありません。電源を入れ直してください。",
    tipStill: "UPDATING と表示されるか暗いままなら、まだアップデートモードです。「FM-1 を探す」を押して再開してください。",
    tipBrick: "まったく起動しない場合は、このページの「FM-1 が起動しないとき」を見てください。",
    endPartialH: "Melodee はインストールされ、m-bootloader はされていません",
    endPartial: "Melodee {v} はインストールされました。m-bootloader は書き込めず、ほかには何も変更されていません。あとでこのページからもう一度試せます。",
    btnEditor: "エディターを開く", btnManual: "マニュアルを読む", btnClose: "閉じる", btnResume: "インストールを再開",
    btnRetry: "もう一度", btnSetup: "セットアップを完了",
    e_resfetch: "ダウンロードできませんでした：{d}", e_resfile: "ダウンロードしたファイルが壊れています。ページを再読み込みしてから「セットアップを完了」を押してください。",
    e_resnoanswer: "再起動のあと、FM-1 が応答しませんでした。ケーブルを確かめてから「セットアップを完了」を押してください。",
    e_reslost: "FM-1 が応答しなくなりました。ケーブルを接続したまま「セットアップを完了」を押してください。",
    e_resnocmd: "FM-1 のファームウェアは書き込みに対応していません。先にこのバージョンをインストールしてください。",
    e_resbuild: "FM-1 はここで選んだものとは別のビルドで動いています。先にこのバージョンをインストールしてください。",
    e_resbusy: "FM-1 が再生中か、Bluetooth がオンです。再生を止めて Bluetooth をオフにしてから「セットアップを完了」を押してください。",
    e_resroom: "FM-1 に書き込む場所を確保できませんでした。保存領域がいっぱいなら、不要なプロジェクトを削除してから「セットアップを完了」を押してください。",
    e_reswrite: "書き込みが途中で止まりました。ケーブルを接続したまま「セットアップを完了」を押してください。",
    e_rescheck: "書き込んだ内容を確認できませんでした。「セットアップを完了」を押して書き込み直してください。",
    e_notfound: "何も書き込まれる前に FM-1 が応答しなくなりました。",
    e_model: "接続されている機器は FM-1 ではありません。",
    e_lost: "USB の接続が切れました。", e_stopped: "FM-1 がデータを要求しなくなりました。",
    e_noloader: "アップデートモードの FM-1 が見つかりません。", e_noreturn: "再起動のあと、FM-1 が戻ってきませんでした。",
    e_mismatch: "書き込みましたが、FM-1 は別のバージョンを報告しています：{d}。", e_badreq: "FM-1 がファイルの範囲外を要求しました。",
    e_open: "この FM-1 は M-VAVE の Open ファームウェアで動いています。先に M-VAVE のファームウェアに戻してください。",
    e_bootrefused: "ファームウェアが m-bootloader を受け付けませんでした。", e_other: "エラー：{m}",
    mResumeH: "インストールが途中で止まったら",
    mResume1: "何も失われていません。インストールが完了するまで、FM-1 はアップデートモードで起動します（画面に UPDATING、または暗いまま）。電源を切っても同じです。",
    mResume2: "FM-1 を接続してこのページを開き、「FM-1 を探す」を押してください。再開を案内し、書き込みの最初から続けます。",
    mStockH: "M-VAVE のファームウェアに戻す",
    mStock1: "M-VAVE 純正の FM-1 ファームウェアを同じケーブルで書き込みます。M-VAVE のブートローダーも元に戻り、Melodee で保存したものは消えることがあります。",
    mStock2: "M-VAVE のファームウェアをダウンロード：", mStock3: "ダウンロードしたファイルを選ぶ：",
    mStock4: "FM-1 を接続し、上の「FM-1 を探す」を押してから、下のボタンを押します。", mStockGo: "M-VAVE のファームウェアを書き込む",
    stockok: "M-VAVE の FM-1 ファームウェアです（{p}）。", stockbad: "M-VAVE の FM-1 ファームウェアではありません。上のリンクのファイルを選んでください。",
    stockfind: "先に上で FM-1 を探してください。",
    mReportH: "不具合を報告する",
    mReport1: "FM-1 がクラッシュしたら、電源を切らないでください。クラッシュの記録は再起動では残りますが、電源を切ると消えます。接続して記録を読み、何をしていたかと一緒に送ってください。",
    mReportGo: "クラッシュの記録を読む", mReportCopy: "コピー", mReportLabel: "クラッシュの記録",
    reportwait: "読み込み中…", reportnone: "応答がありません。FM-1 が Melodee を実行しているか確かめてください。",
    reportok: "読み込みました。コピーして送ってください。", reportcopied: "コピーしました。",
    mBrickH: "FM-1 が起動しないとき",
    mBrick1: "m-bootloader がある場合：電源を切り、PLAY を押しながら電源を入れるとリカバリーメニュー、REC なら USB ダウンロードモードになります。",
    mBrick2: "ない場合は、FM-1 の USB リカバリー手順でパソコンからファームウェアを書き戻せます：",
    mBrickLink: "felucca-bsp のリカバリーツール（英語）",
    credits: "Melodee：Kerem Kilic（Ellic Studio）。Charles Vestal の fm1-base と felucca-bsp を基盤に、Leo Kuroshita（@kurogedelic）、Hügelton Instruments の Felucca から。m-bootloader：Julius Vitkauskas。",
    legal: "ファームウェア：MPL-2.0。エディター：GPL-3.0-only。第三者の利用条件も適用されます。無保証。M-VAVE および FM-1 は各所有者の商標です。",
    details: "詳細", sec: "{n} 秒", min: "{n} 分",
  },
};

let lang = (() => {
  try { const s = localStorage.getItem(LANG); if (s === "en" || s === "ja") return s; } catch (_) { /* none kept */ }
  return (navigator.language || "en").toLowerCase().startsWith("ja") ? "ja" : "en";
})();
const t = (k, vars = {}) => (TEXT[lang][k] ?? TEXT.en[k] ?? k).replace(/\{(\w+)\}/g, (_, n) => vars[n] ?? "");

const st = {
  meta: null, image: null, boot: null, stock: null,  // the release, its images, a chosen stock package
  access: null, dev: null,                            // the MIDI access; the FM-1 found: { id, kind, version, text }
  running: false, run: null,                          // the install under way (its screen's state)
  connErr: null, stockErr: null, demo: null,
};
const ver = () => (st.meta ? st.meta.version : "");
const buildKey = () => [build(),st.meta.version,st.meta.commit || "",st.meta.product,st.meta.sha256?.dual || ""].join(":");
const pendingInstall = () => { try { return JSON.parse(localStorage.getItem(MARK)); } catch (_) { return null; } };
const mark = (on) => { try { if (on) localStorage.setItem(MARK,JSON.stringify({key:buildKey(),label:ver()})); else localStorage.removeItem(MARK); } catch (_) { /* not kept */ } };

/* ---- language ---- */
function applyLang() {
  document.documentElement.lang = lang;
  document.querySelectorAll("[data-t]").forEach((e) => { e.textContent = t(e.dataset.t, { v: st.meta?.boot?.version ?? "" }); });
  $("lang").textContent = t("other");
  $("lang").lang = lang === "ja" ? "en" : "ja";
  document.querySelectorAll("[data-release-t]").forEach(e=>{e.textContent=releaseText[lang][e.dataset.releaseT] || "";});
  leaving?.applyLang();
  render();
}
$("lang").addEventListener("click", () => {
  lang = lang === "ja" ? "en" : "ja";
  try { localStorage.setItem(LANG, lang); } catch (_) { /* not kept */ }
  applyLang();
});

/* ---- the release: release.json, its package and m-bootloader, each checked ---- */
const sha256 = async (raw) => [...new Uint8Array(await crypto.subtle.digest("SHA-256", raw))].map((b) => b.toString(16).padStart(2, "0")).join("");
async function fetchBytes(url) {
  const r = await fetch(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}
const buildIds = ["dual", "rc-dual", "nightly-dual"];
const build = () => buildIds.find(k => $("build-" + k).checked) || "dual";
const selection = () => {
  const kind = build(), dash = kind.indexOf("-");
  return dash < 0 ? (meta.releases || []).find(r => r.version === $("release").value) || meta : meta[kind.slice(0,dash)];
};
let buildEpoch = 0;
const images = new Map();
/* the build's resource region (fm1res.js), <package>-res.bin beside its package, fetched as the package is (the
   commit keeps a nightly's copy from a stale cache): null when the build has none (before 1.6). A failed fetch is
   forgotten, so Finish setup fetches again. */
const regions = new Map();
async function fetchRegion(chosen) {
  const url = chosen.pkg.dual.replace(/(\.fwsc)?$/, "-res.bin") + (chosen.commit ? "?commit=" + chosen.commit : "");
  const r = await fetch(url, { cache: "no-cache" });
  if (r.status === 404) return null;
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}
function resRegion() {
  if (DEMO) return Promise.resolve(st.demoRes);
  const key = buildKey();
  if (!regions.has(key)) {
    const region = fetchRegion(st.meta);
    regions.set(key, region);
    region.catch(() => { if (regions.get(key) === region) regions.delete(key); });
  }
  return regions.get(key);
}
function releaseOptions() {
  const selected = $("release").value || meta.version;
  $("release").replaceChildren(...(meta.releases || [meta]).map(r => Object.assign(document.createElement("option"), {
    value:r.version, textContent:(r.version === meta.version ? "Latest · " : "") + r.version, selected:r.version === selected
  })));
  $("advanced-installs").hidden = !meta.rc && !meta.nightly;
  $("rc-options").hidden = !meta.rc;$("nightly-options").hidden = !meta.nightly;
  if (meta.rc) $("rc-version").textContent = meta.rc.version;
  if (meta.nightly) $("nightly-info").textContent = `${meta.nightly.commit.slice(0,7)} · ${meta.nightly.builtAt}`;
}
async function loadRelease() {
  if (st.running) return;
  const epoch = ++buildEpoch, chosen = selection();
  if (DEMO) return demoRelease(chosen);
  st.image = null;st.boot = null;
  st.meta = {...chosen,boot:meta.boot};render();
  $("pagefail").hidden = true;
  try {
    const key = [build(),chosen.version,chosen.commit || "",chosen.sha256?.dual || ""].join(":");
    if (!images.has(key)) {
      const raw = await fetchBytes(chosen.pkg.dual + (chosen.commit ? "?commit=" + chosen.commit : ""));
      if ((chosen.sha256?.dual && await sha256(raw) !== chosen.sha256.dual) || productOf(raw) !== chosen.product) throw fail("badpkg",t("badpkg"));
      images.set(key,logicalImage(raw));
    }
    if (epoch !== buildEpoch) return;
    st.image = images.get(key);
    resRegion().catch(() => { /* Install writes the firmware anyway; its end screen offers Finish setup */ });
    if (st.dev) { st.dev = {id:st.dev.id,res:st.dev.res,...describeDevice(st.dev.id,st.meta.product)};offerSetup(); }
    if (meta.boot) {
      try {
        const raw = await fetchBytes(meta.boot.pkg);
        if (await sha256(raw) === meta.boot.sha256 && productOf(raw) === "FM-1_099" && epoch === buildEpoch) st.boot = {image:logicalImage(raw),version:meta.boot.version};
      } catch (_) { /* as Saadet: a missing or damaged bootloader is never offered */ }
    }
    if (epoch !== buildEpoch) return;
  } catch(e) { if(epoch === buildEpoch) pageFail(t("loadfail",{m:e.message})); }
  if(epoch === buildEpoch) applyLang();
}
buildIds.forEach(k => $("build-" + k).addEventListener("change",loadRelease));
$("release").addEventListener("change",() => {if(st.running)return;$("build-dual").checked=true;loadRelease();});

/* the demo: a simulated FM-1 (fakefm1.js) and small images of its own; nothing is fetched or written */
function demoImage(n, seed) { const b = new Uint8Array(n); for (let i = 0; i < n; i++) b[i] = (i * 131 + seed * 7 + (i >> 8)) & 0xFF; return b; }
function demoRelease(meta) {
  const version = meta?.version || "1.0";
  const product = meta?.product || "FM-1_9010000";
  st.meta = { name: "Melodee", version, product, boot: { version: meta?.boot?.version || "1.2" } };
  st.image = demoImage(0x30000, 1);
  st.boot = { image: demoImage(0x4800, 2), version: st.meta.boot.version };
  st.demoRes = demoImage(32 + 0x3000, 3);           /* a resource region: MRS1, length, a CRC the device reports */
  new DataView(st.demoRes.buffer).setUint32(0, 0x3153524d, true);
  new DataView(st.demoRes.buffer).setUint32(4, 0x3000, true);
  $("demo").hidden = false;
}
async function demoAccess() {
  if (DEMO === "none") return { inputs: new Map(), outputs: new Map(), sysexEnabled: true };
  if (!st.demo) {
    // fakeAccess is bundled for the exact same Saadet demo.
    const p = st.meta.product;
    const opts = {
      stock: { identity: "FM-1_016" }, melodee: { identity: "FM-1_9000900" }, current: { identity: p, resInstalled: true },
      setup: { identity: p }, update: { identity: "FM-1_016", state: "loader" },
      interrupt: { identity: "FM-1_016", interruptAt: 0.45 }, open: { identity: "FM1_OFFICIAL_OPEN" },
    }[DEMO] || { identity: "FM-1_016" };
    st.demo = fakeAccess({ images: { firmware: st.image, boot: st.boot.image, res: st.demoRes }, after: p,
                           loader:MELODEE_LOADER, ...opts });
  }
  return st.demo.access;
}

function pageFail(text, demoLink = false) {
  $("pagefail").hidden = false;
  $("pagefailtext").textContent = text + " ";
  if (demoLink) {
    const a = document.createElement("a");
    a.href = "?demo";
    a.textContent = "?demo";
    $("pagefailtext").append(a);
  }
  $("find").disabled = true;
}

/* ---- finding the FM-1 ---- */
async function midi() {
  if (DEMO) return demoAccess();
  if (!navigator.requestMIDIAccess) throw fail("nomidi", "no Web MIDI");
  try { return await navigator.requestMIDIAccess({ sysex: true }); }
  catch (_) { throw fail("denied", "MIDI not allowed"); }
}

let finding = false;
async function connect() {
  if (finding || st.running) return;
  finding = true;
  st.connErr = null;
  render();
  try {
    const release = await hold();
    if (!release) throw fail("busy", "busy");
    let dev, res = null;
    try {
      st.access = await midi();dev = await new SteadyUpdater(st.access).find();
      if (dev) {
        dev.link.close();
        if (["this", "dev"].includes(describeDevice(dev.id, st.meta.product).kind) && await resRegion().catch(() => null))
          res = await probeRes(dev);
      }
    }
    finally { release(); }
    if (!dev) { st.dev = null; throw fail("notfound", "not found"); }
    st.dev = { id: dev.id, res, ...describeDevice(dev.id, st.meta.product) };
  } catch (e) {
    st.dev = null;st.connErr = ["nomidi", "denied", "notfound", "busy", "pending"].includes(e.code) ? e.code : "notfound";
  }
  finding = false;
  render();
  offerSetup();
}
/* a running Melodee's resource region (editor command 82): { installed, length, crc } or null (no answer) */
async function probeRes(dev) {
  const link = new BackupConnection(dev.link.input, dev.link.output);
  try { return resStatus(await link.request([RES_COMMAND, [0]], { timeout: 1500 })); }
  catch (_) { return null; }
  finally { link.close(); }
}
/* Finish setup, offered when the FM-1 runs the selected build (or a development one) without its region and that
   build has one: the same length and CRC-32 as the device wants */
async function offerSetup() {
  const dev = st.dev, s = dev?.res;
  let ok = false;
  if (s && !s.installed && ["this", "dev"].includes(dev.kind)) {
    try { const h = resHeader(await resRegion()); ok = h.length === s.length && h.crc === s.crc; }
    catch (_) { /* nothing to offer */ }
  }
  if (dev && st.dev === dev && !!dev.setup !== ok) { dev.setup = ok; render(); }
}
$("find").addEventListener("click", connect);
$("mboot").addEventListener("change", render);
$("finish").addEventListener("click", () => finishSetup());

/* ---- the page's state, drawn ---- */
function render() {
  if (!st.meta) return;
  $("hver").textContent = ver();
  const dev = st.dev, plan = currentPlan();
  /* step 1 */
  const fb = $("find");
  fb.replaceChildren();
  if (finding) { const s = document.createElement("span"); s.className = "spin"; fb.append(s, t("finding")); }
  else fb.append(t(dev || st.connErr ? "findAgain" : "find"));
  fb.disabled = finding || st.running || !st.image;
  fb.classList.toggle("primary", !dev);
  $("connmsg").textContent = st.connErr ? t(st.connErr) : "";
  $("device").hidden = !dev;
  if (dev) {
    const words = { loader: "devLoader", this: "devThis", melodee: "devMelodee", dev: "devDev", stock: "devStock",
                    open: "devOpen", other: "devOther", model: "devModel" }[dev.kind];
    $("devwhat").textContent = t(words, { v: dev.version, id: dev.text || dev.id.text });
    const chip = dev.kind === "loader" ? ["chipResume", "warn"] : dev.setup ? ["chipSetup", "warn"]
      : dev.kind === "this" ? ["chipCurrent", "ok"] : plan ? ["chipReady", "ok"] : ["chipBlocked", "bad"];
    $("devchip").textContent = t(chip[0]);
    $("devchip").className = "chip " + chip[1];
  }
  /* step 2: only with m-bootloader's package, never for M-VAVE's firmware */
  $("st-options").hidden = !st.boot;
  /* step 3 */
  const go = $("go");
  if (!dev || !plan) {
    go.textContent = t(dev?.kind === "loader" ? "goResume" : "goInstall", { v: ver() });
    go.disabled = true;
    $("gohint").hidden = !!dev;
    $("plantext").textContent = !dev ? t("s3p") : t(dev.kind === "open" ? "planOpen" : "planModel", { id: dev.id.text });
  } else {
    const label = plan.resume ? "goResume" : !plan.firmware ? "goBoot" : dev.kind === "this" ? "goReinstall"
      : ["melodee", "dev"].includes(dev.kind) ? "goUpdate" : "goInstall";
    go.textContent = t(label, { v: ver() });
    go.disabled = st.running || finding || !st.image;
    $("gohint").hidden = true;
    const words = dev.setup ? "planSetup" : plan.resume ? "planResume" : !plan.firmware ? "planBoot"
      : dev.kind === "this" ? "planThis" : ["melodee", "dev"].includes(dev.kind) ? "planUpdate" : "planInstall";
    $("plantext").textContent = t(words, { v: ver() });
  }
  /* Finish setup: the region alone, the install button second */
  $("finish").hidden = !dev?.setup;
  $("finish").disabled = st.running || finding;
  go.classList.toggle("primary", !dev?.setup);
  $("st-connect").dataset.state = dev ? "done" : "active";
  $("st-options").dataset.state = dev && plan ? "active" : "todo";
  $("st-install").dataset.state = dev && plan ? "active" : "todo";
  $("release").disabled = $("mboot").disabled = st.running || finding;
  buildIds.forEach(k => { $("build-" + k).disabled = st.running || finding; });
  leaving?.lock(st.running || finding);
  /* an unfinished install: kept from an earlier visit, or the FM-1 itself in update mode */
  let pending = false;
  try { pending = !!localStorage.getItem(MARK); } catch (_) { /* unknown */ }
  if (pending && dev && dev.kind !== "loader" && !st.running) { mark(false); pending = false; }   /* it finished after all */
  $("resumebanner").hidden = !(pending || dev?.kind === "loader");
}

function currentPlan() {
  if (!st.dev) return null;
  return planFor(st.meta.product === "FM-1_900" && st.dev.kind === "this" ? "dev" : st.dev.kind, !!st.boot && $("mboot").checked);
}

/* ---- before writing: the rules, and the backup (asked every time) ---- */
function preflight(plan) {
  const d = $("preflight");
  $("pfboot").hidden = !plan.boot;
  $("pfboot").lastElementChild.textContent = t(plan.firmware ? "pfboot" : "pfbootonly");
  const askBackup = plan.firmware && !plan.resume;   /* in update mode there is no firmware running to back up */
  $("pfcheck").hidden = !askBackup;
  $("pfok").checked = false;
  $("pfgo").disabled = askBackup;
  d.showModal();
  (askBackup ? $("pfok") : $("pfgo")).focus();
  /* answered by the button itself (submit), not the dialog's close event, which a browser may hold back while
     the page is not drawn */
  return new Promise((resolve) => {
    const form = d.querySelector("form");
    const done = (go) => {
      form.removeEventListener("submit", submit);
      d.removeEventListener("cancel", cancel);
      d.close();
      resolve(go);
    };
    const submit = (e) => { e.preventDefault(); if (e.submitter?.value !== "go" || !askBackup || $("pfok").checked) done(e.submitter?.value === "go"); };
    const cancel = (e) => { e.preventDefault(); done(false); };
    form.addEventListener("submit", submit);
    d.addEventListener("cancel", cancel);
  });
}
$("pfok").addEventListener("change", () => { $("pfgo").disabled = !$("pfok").checked; });

/* ---- the install's screen ---- */
const ICON = { done: "#i-check", paused: "#i-pause", safe: "#i-info", restart: "#i-alert", partial: "#i-alert", setup: "#i-alert" };
const FAVICON = (c) => `data:image/svg+xml,${encodeURIComponent(`<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='4' fill='${c}'/></svg>`)}`;
const baseTitle = document.title;

function log(line) {
  const el = $("log"), atEnd = el.scrollHeight - el.scrollTop - el.clientHeight < 24;
  el.append(line + "\n");
  if (atEnd) el.scrollTop = el.scrollHeight;
}

function phasesFor(plan) {
  return [plan.firmware && !plan.resume && "prepare", plan.firmware && "write", plan.firmware && "restart", plan.res && "setup",
          plan.boot && "boot"].filter(Boolean);
}

function openRun(plan, isStock) {
  const r = { plan, isStock, pct: 0, phase: null, writeAt: 0, lastAt: Date.now(), hiddenPct: 0, list: phasesFor(plan) };
  st.run = r;
  const d = $("run");
  $("legacy-actions").hidden = true;
  d.dataset.state = "running";
  $("end").hidden = true;
  $("log").textContent = "";
  $("backnote").hidden = $("stall").hidden = true;
  $("eta").textContent = "";
  $("runtitle").textContent = t(isStock ? "runStock" : plan.resume ? "runResume" : plan.firmware ? "runInstall"
    : plan.boot ? "runBoot" : "runSetup", { v: ver() });
  document.querySelectorAll("#phases li").forEach((li) => { li.hidden = !r.list.includes(li.dataset.ph); li.className = ""; });
  setPct(0);
  setStep(plan.resume ? "stFind" : plan.firmware || plan.boot ? "stStart" : "stRes", r.list[0]);
  $("favicon").href = FAVICON("#d08a00");
  if (!d.open) d.showModal();
}
$("run").addEventListener("cancel", (e) => { if (st.running) e.preventDefault(); });

function setPct(p) {
  const r = st.run;
  r.pct = Math.max(r.pct, Math.min(100, Math.floor(p)));
  $("pct").textContent = r.pct;
  $("barfill").style.width = r.pct + "%";
  $("bar").setAttribute("aria-valuenow", r.pct);
  document.title = document.visibilityState === "hidden" ? t("titleHidden", { p: r.pct }) : t("titleRun", { p: r.pct });
}

function setStep(key, phase) {
  const r = st.run;
  $("runstep").textContent = t(key);
  if (phase && phase !== r.phase) {
    r.phase = phase;
    const at = r.list.indexOf(phase);
    document.querySelectorAll("#phases li").forEach((li) => {
      const i = r.list.indexOf(li.dataset.ph);
      li.className = i < 0 ? "" : i < at ? "past" : i === at ? "now" : "";
    });
  }
  $("bar").classList.toggle("indet", phase === "boot" && !r.plan.firmware);
}

// fm1ota.js's steps -> the screen. The firmware fills the bar, or most of it when m-bootloader follows.
function stepper(pm) {
  const r = st.run, share = r.plan.boot ? (r.plan.firmware ? 0.94 : 0) : 1;
  return (k, a, b) => {
    if (k !== "res" || a === b || a % 8192 < 256) log(`${k} ${a ?? ""}${b != null ? " " + b : ""}`);
    r.lastAt = Date.now();
    $("stall").hidden = true;
    if (k === "start") setStep("stStart", "prepare");
    else if (k === "verify") { setStep("stVerify", "prepare"); setPct(pm.verify(b) * share); }
    else if (k === "loader") setStep("stLoader", "prepare");
    else if (k === "write") {
      if (!r.writeAt) r.writeAt = Date.now() - 1;
      setStep("stWrite", "write");
      setPct(pm.writing(b) * share);
      const s = remaining(b, pm.write, Date.now() - r.writeAt);
      $("eta").textContent = s == null ? "" : s >= 60 ? t("etaMin", { n: Math.ceil(s / 60) }) : t("etaSec");
    } else if (k === "reboot") { $("eta").textContent = ""; setStep("stReboot", "restart"); setPct(99 * share); }
    else if (k === "res") { setStep("stRes", "setup"); if (!r.plan.firmware) setPct((a / b) * 100); }
    else if (k === "done" && r.plan.res) setStep("stRes", "setup");
    else if (k === "done" && r.plan.boot) setStep("stBootWait", "boot");
    else if (k === "bootstart") { setStep("stBoot", "boot"); setPct(share ? 96 : 30); }
    else if (k === "bootwrite") { setStep("stBoot", "boot"); setPct(share ? 98 : 70); }
  };
}

// no progress for a while during a transfer: say what the page is waiting for (UI only)
setInterval(() => {
  const r = st.run;
  if (!st.running || !r || !["prepare", "write"].includes(r.phase)) return;
  $("stall").hidden = Date.now() - r.lastAt < 12000;
}, 1000);

function away(ms) {
  const s = Math.round(ms / 1000);
  return s < 60 ? t("sec", { n: s }) : t("min", { n: Math.round(s / 60) });
}
const onHidden = () => {
  const r = st.run;
  if (!r) return;
  r.hiddenPct = r.pct;
  document.title = t("titleHidden", { p: r.pct });
};
const onVisible = (ms) => {
  const r = st.run;
  if (!r || !st.running) return;
  document.title = t("titleRun", { p: r.pct });
  if (ms < 1500) return;
  const kept = r.pct > r.hiddenPct || r.phase === "restart" || r.phase === "boot";
  $("backtext").textContent = t(kept ? "backKept" : "backSlow", { s: away(ms) });
  $("backnote").hidden = false;
};

function endRun(state, err) {
  const r = st.run, plan = r.plan, v = { v: ver() };
  const d = $("run");
  d.dataset.state = state;
  $("end").hidden = false;
  $("endicon").setAttribute("href", ICON[state]);
  const tips = [], acts = [];
  const button = (key, primary, fn) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "btn" + (primary ? " primary" : "");
    b.textContent = t(key);
    b.addEventListener("click", fn);
    acts.push(b);
  };
  const link = (key, href, primary) => {
    const a = document.createElement("a");
    a.className = "btn" + (primary ? " primary" : "");
    a.href = href;
    a.textContent = t(key);
    a.style.textDecoration = "none";
    acts.push(a);
  };
  const why = (e) => e ? (e.code && TEXT.en["e_" + e.code] ? t("e_" + e.code, { d: e.detail }) : t("e_other", { m: e.message })) : "";
  const reason = why(err);
  /* the resource region's write (pushResources): null when none ran, { ok } or { ok: false, error } */
  const resFailed = r.res && !r.res.ok;
  const setupAgain = (primary) => button("btnSetup", primary, () => { closeRun(); finishSetup(); });
  let title, text;
  if (state === "done") {
    if (r.isStock) { title = t("endStockH"); text = t("endStock"); }
    else if (!plan.firmware && plan.res) { title = t("endSetupDoneH"); text = t("endSetupDone"); tips.push(t("tipResRestart")); }
    else if (!plan.firmware) { title = t("endBootH"); text = t("endBoot", v); tips.push(t("tipBoot")); }
    else {
      title = t(plan.boot ? "endDoneBootH" : "endDoneH", v);
      text = t("endDone", v);
      if (r.res?.ok) tips.push(t("tipResOk"));
      tips.push(t("tipEditor"));
      if (plan.boot) tips.push(t("tipBoot"));
    }
    if (!r.isStock) { link("btnEditor", "../editor/", true); link("btnManual", "../manual/"); }
    button("btnClose", r.isStock, closeRun);
    setPct(100);
    document.title = t("titleDone");
    $("favicon").href = FAVICON("#1f8a46");
  } else if (state === "paused") {
    title = t("endPausedH"); text = t("endPaused");
    tips.push(reason, t("tipCable"), t("tipApps"), t("tipResume"));
    st.dev = { id: { text: "ota-FM-1", model: "ota-FM-1" }, kind: "loader" };
    button("btnResume", true, () => { closeRun(); start(); });
    button("btnClose", false, closeRun);
  } else if (state === "restart") {
    title = t("endRestartH"); text = err?.code === "mismatch" ? reason : t("endRestart");
    tips.push(t("tipStill"), t("tipBrick"));
    st.dev = null;
    button("btnClose", true, closeRun);
  } else if (state === "partial") {
    title = t("endPartialH"); text = t("endPartial", v);
    if (reason) tips.push(reason);
    if (resFailed) { tips.push(t("endSetup", v), why(r.res.error)); setupAgain(true); }
    button("btnClose", !resFailed, closeRun);
  } else if (state === "setup") {
    title = t("endSetupH"); text = t("endSetup", v);
    tips.push(why(r.res?.error));
    if (plan.boot) tips.push(t("tipBoot"));
    setupAgain(true);
    button("btnClose", false, closeRun);
  } else {
    title = t("endSafeH"); text = t("endSafe");
    if (reason) tips.unshift(reason);
    tips.push(t("tipCable"), t("tipApps"));
    button("btnRetry", true, () => { closeRun(); connect(); });
    button("btnClose", false, closeRun);
  }
  $("endtitle").textContent = title;
  $("endtext").textContent = text;
  $("endtips").replaceChildren(...tips.filter(Boolean).map((x) => Object.assign(document.createElement("li"), { textContent: x })));
  $("endactions").replaceChildren(...acts);
  if (state !== "done") { document.title = baseTitle; $("favicon").href = FAVICON(state === "safe" ? "#777" : "#d08a00"); }
  (acts[0] || $("endtitle")).focus();
}

function closeRun() {
  $("run").close();
  document.title = baseTitle;
  render();
}

/* ---- the install ---- */
async function resumeWrite(up, image, product, step) {
  const ota = await up.waitFor(IS_OTA,8000);
  if (!ota) throw fail("noloader","FM-1 in update mode not found");
  ota.link.close();
  if (ota.id.text !== MELODEE_LOADER) throw fail("wrongloader",`This package requires ${MELODEE_LOADER}; found ${ota.id.text}`,ota.id.text);
  if (!await up.resume(image,step,product,MELODEE_LOADER)) throw fail("stopped","The update did not finish");
}
/* the resource region (firmware/src/res.c) after the firmware, or alone (Finish setup): the build's region through
 * editor command 82. Never fails the install: without it the FM-1 runs Melodee Pixel and INIT sounds, and the end
 * screen says so and offers Finish setup. -> null (the build has none), { ok: true, how } or { ok: false, error } */
async function pushResources(up, step) {
  let region;
  try { region = await resRegion(); }
  catch (e) { log(`resources: ${e.message}`); return { ok: false, error: fail("resfetch", e.message, e.message) }; }
  if (!region) return null;
  for (let tries = 0; tries < 20; tries++) {
    const dev = await up.find(id => id.model === "FM-1");
    if (dev) {
      const input = dev.link.input, output = dev.link.output; dev.link.close();
      const link = new BackupConnection(input, output);
      try {
        const how = await installResources((r, o) => link.request(r, o), region, (done, total) => step("res", done, total));
        log(`resources: ${how}`);
        return { ok: true, how };
      } catch (e) {
        log(`resources: ${e.message}`);
        return { ok: false, error: e.code ? e : fail("reslost", e.message) };   /* (no code: the request timed out) */
      } finally { link.close(); }
    }
    await sleep(500);
  }
  log("resources: the FM-1 did not answer");
  return { ok: false, error: fail("resnoanswer", "no answer") };
}

/* Finish setup: the resource region alone, for an FM-1 that runs the build without it (the end screen's retry, or
 * offered on Find). No preflight: the firmware is not touched, and an interrupted write only leaves the fallbacks. */
async function finishSetup() {
  if (st.running || finding) return;
  st.running = true;
  render();
  const release = await hold({ leaveText: t("leave"), onHidden, onVisible });
  if (!release) {
    st.running = false;
    if (release === null) st.connErr = "busy";
    render();
    return;
  }
  openRun({ firmware: false, resume: false, boot: false, res: true }, false);
  let res;
  try {
    if (!st.access) st.access = await midi();
    res = await pushResources(new SteadyUpdater(st.access, log), stepper(null));
  } catch (e) { log(String(e?.stack || e)); res = { ok: false, error: e }; }
  st.run.res = res || { ok: false, error: fail("resbuild", "The selected build has no resources") };
  st.running = false;
  release();
  const dev = st.dev;
  if (dev?.res && st.run.res.ok) { dev.res = { ...dev.res, installed: true }; dev.setup = false; }
  endRun(st.run.res.ok ? "done" : "setup", null);
  render();
}

async function start(isStock = false) {
  if (st.running || finding) return;
  const plan = isStock ? { firmware: true, resume: st.dev?.kind === "loader", boot: false } : currentPlan();
  if (!plan) return;
  const pending = pendingInstall();
  if (plan.resume && pending?.key && pending.key !== buildKey()) {
    st.connErr = "pending";render();return;
  }
  st.running = true;                                  /* from the first click: a second one starts nothing */
  render();
  let release;
  try {
    release = await hold({ leaveText:t("leave"),onHidden,onVisible });
    if (release && !await preflight(plan)) { release();release=false; }
  } catch (e) { st.running=false;render();throw e; }
  if (!release) {
    st.running = false;
    if (release === null) st.connErr = "busy";
    render();
    return;
  }
  render();
  /* Setup follows the firmware when the build has a resource region (a failed fetch shows there, with its retry) */
  if (plan.firmware && !isStock) plan.res = (await resRegion().catch(() => undefined)) !== null;
  openRun(plan, isStock);
  const image = isStock ? st.stock.image : st.image;
  const product = isStock ? st.stock.product : st.meta.product;
  const step = stepper(progressModel(image, plan.resume));
  let phase = "start", state = "done", error = null;
  if (plan.firmware) mark(true);
  try {
    if (!st.access) st.access = await midi();
    const up = new SteadyUpdater(st.access,log);
    if (plan.firmware) {
      if (plan.resume) { phase = "staged"; await resumeWrite(up, image, product, step); }
      else {
        await up.install(image, product, (k, a, b) => { if (k === "loader") phase = "staged"; step(k, a, b); },
                         MELODEE_LOADER);
      }
      phase = "done";
      mark(false);
      if (plan.res) {
        st.run.res = await pushResources(up, step);
        if (plan.boot) setStep("stBootWait", "boot");
      }
    }
    if (plan.boot) { phase = "boot"; await up.installBoot(st.boot.image, product, step); }
  } catch (e) {
    {
      state = afterFailure(e?.code, phase);
      error = e;
      if (state === "safe") mark(false);
      log(String(e?.stack || e));
    }
  }
  st.running = false;
  release();
  if (state === "done" && st.run.res && !st.run.res.ok) state = "setup";   /* the firmware runs; its region doesn't */
  if (state === "done" || state === "setup") st.dev = null;   /* what it runs now is shown on the next Find */
  endRun(state, error);
  render();
}
$("go").addEventListener("click", () => start(false));

/* ---- the crash report: the firmware's log readout (F0 7D 4C 47 F7 asks; a frame a line, "end" last) ---- */
const LG = [0x7D, 0x4C, 0x47];
$("reportgo").addEventListener("click", async () => {
  const say = k => { $("reportstatus").textContent=t(k); };
  if (st.running || finding) return;
  const release=await hold();
  if (!release) { say("busy");return; }
  st.running=true;render();$("reportgo").disabled=true;say("reportwait");
  const lines=[];let dev;
  try {
    st.access=st.access || await midi();
    dev=await new SteadyUpdater(st.access).find(id=>!IS_OTA(id));
    if (dev) {
      lines.push(`${dev.id.text} (${dev.name}), ${new Date().toISOString()}`);
      dev.link.drain();dev.link.send([0xF0,...LG,0xF7]);
      const end=Date.now()+4000;
      while(Date.now()<end) {
        const p=await dev.link.read(end-Date.now());
        if(!p) break;
        if(p.length<5 || p[1]!==LG[0] || p[2]!==LG[1] || p[3]!==LG[2]) continue;
        const text=String.fromCharCode(...p.slice(4,-1));
        if(text==="end") break;
        lines.push(text);
      }
    }
    if(lines.length<2) { say("reportnone");return; }
    $("report").value=lines.join("\n");$("report").hidden=$("reportcopy").hidden=false;say("reportok");
  } catch(e) { say(e.code && TEXT.en[e.code] ? e.code : "reportnone"); }
  finally {dev?.link.close();release();st.running=false;$("reportgo").disabled=false;render();}
});
$("reportcopy").addEventListener("click", async () => {
  try { await navigator.clipboard.writeText($("report").value); $("reportstatus").textContent = t("reportcopied"); }
  catch (_) { $("report").select(); }
});

const leaving = setupLeaving({
  getLanguage:()=>lang, isBusy:()=>st.running || finding,
  setBusy:on=>{st.running=on;render();},log
});
const releaseText = {
 en:{releaseTitle:"Stable version",dual:"Standard",advancedTitle:"Advanced installs (RC · Nightly)",rcTitle:"Release candidate","rc-dual":"Release candidate",rcWhy:"A test build of the next release.",nightlyTitle:"Nightly","nightly-dual":"Nightly",nightlyWhy:"The current development build. Back up before updating."},
 ja:{releaseTitle:"安定版のバージョン",dual:"標準",advancedTitle:"詳細（RC・Nightly）",rcTitle:"リリース候補","rc-dual":"リリース候補",rcWhy:"次のリリースのテスト版です。",nightlyTitle:"Nightly","nightly-dual":"Nightly",nightlyWhy:"現在の開発版です。先にバックアップしてください。"}
};
/* ---- start ---- */
(async () => {
  releaseOptions();
  applyLang();
  await loadRelease();
  applyLang();
  if (!st.image) return;
  /* MIDI already allowed (a returning visit, or a reload during an install): look without asking */
  let granted = !!DEMO && DEMO !== "none" && params.has("auto");
  try {
    if (!DEMO && navigator.permissions && navigator.requestMIDIAccess) {
      granted = (await navigator.permissions.query({ name: "midi", sysex: true })).state === "granted";
    }
  } catch (_) { /* unknown: wait for the button */ }
  if (granted) connect();
})();
