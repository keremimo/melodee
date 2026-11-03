// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Existing Melodee stock/Transit backup and loader safeguards, integrated with Saadet's page.
import {createInstallSession} from './session.js';
export function setupLeaving({getLanguage,isBusy,setBusy,log}) {
const $=id=>document.getElementById(id);
const TEXT = {
  ja: {
    intro: "FM-1 を USB で接続してください。（Chrome か Edge で使えます）",
    warn: "書き込み中は USB を抜かないでください。",
    install: "インストール", other: "English",
    releaseTitle: "安定版のバージョン", latest: "最新版",
    advancedTitle: "詳細なインストール（RC・Nightly）",
    rcTitle: "リリース候補", "rc-dual": "RC",
    "rc-dualTag": "リリース候補",
    rcWhy: "次のリリースのテスト版です。標準を選んでインストールすると安定版へ戻せます。",
    nightlyTitle: "Nightly（next）", "nightly-dual": "Nightly",
    "nightly-dualTag": "nightly",
    nightlyWhy: "next の開発版です。インストール前にエディタで全体バックアップを保存してください。標準版を選ぶとリリース版へ戻せます。",
    dual: "標準", dualTag: "標準",
    editor: "エディタ（インストール後、パソコンから音色やシーケンスを編集）",
    direct: "USB ケーブルはパソコンに直接つないでください。Probe や Transporter などの機材は不要です。",
    brick: "起動保護付きファームウェアの更新が中断した場合は、電源を入れ直すか、OCT− だけを押しながら電源を入れてください。キーのライトが短く3回点滅したら更新モードです。USB を接続し直して、もう一度インストールしてください。以前のファームウェアや長い1回の点滅の場合は、USB 復旧手順を確認してください:",
    recoveryLink: "USB 復旧手順とダウンロード",
    beta: "Melodee の不具合は GitHub で知らせてください。このカスタムファームウェアを使ったことで FM-1 が壊れたり不具合が起きたりしても、作者は一切の責任を負いません。自己責任でお使いください。",
    transitTitle: "別のファームウェアへ切り替える",
    transitWhy: "別のカスタムファームウェアへ移る前に、TransitFM1をインストールしてください。切り替えのための中継用ファームウェアで、音は出ません。Baud Girlへ移る場合は、先に公式V15をインストールし、そこから専用インストーラーを使ってください。",
    transitFirst: "下のボタンでTransitFM1 0.1.3をインストールします。旧版0.1と配布中止の0.1.2は使わないでください。",
    transitNext: "FM-1にTRANSITと表示されたら、移行先のファームウェア専用インストーラーでインストールします。",
    transitGo: "バックアップしてTransitFM1をインストール",
    transitGoNoBackup: "バックアップせずTransitFM1をインストール",
    transitConfirm: "全体バックアップをパソコンに保存できましたか？ TransitFM1をインストールします。次は移行先のインストーラーを使ってください。",
    transitConfirmNoBackup: "バックアップを作成せずTransitFM1をインストールしますか？ Melodeeの曲・音色・設定が失われる可能性があり、バックアップなしでは復元できません。続けますか？",
    transitLoading: "TransitFM1をダウンロードして検証中",
    transitDone: "TransitFM1をインストールしました。FM-1のTRANSIT表示を確認し、移行先のインストーラーを開いてください。",
    transitSafety: "TransitFM1は移行先への書き込み中断に備えますが、最後のセクターは保護できない場合があります。Baud Girlなど書き込み範囲を超えるファームウェアには公式V15を経由してください。充電し、MIDIを送信するDAWなどを閉じ、書き込み中はUSBを抜かないでください。中断時は同じファームウェアで再開してください。",
    transitAbout: "TransitFM1の使い方・復旧手順", transitSource: "Quixotic7によるソース・ライセンス",
    back: "M-VAVEから公式FM-1 V15を取得し、未変更のFM-1.fwscを選んでください。ファイルはこのブラウザ内で検証し、サーバーへ送りません。ファイルは変更せず、M-VAVE公式の書き込みモードで書き込みます。",
    stockTitle: "公式V15に戻す", stockFile: "公式FM-1.fwsc", stockGo: "バックアップして公式V15へ戻す",
    stockBackup: "現在の曲・保存曲・音色バンク・サンプル・設定を先に保存します。バックアップできない場合は、Melodeeを更新するか、データ消失を了承してバックアップを省略できます。復元はMelodeeを再インストールしてエディタから行います。",
    stockSkipBackup: "バックアップを省略する（Melodeeの曲・音色・設定が失われる可能性があります）",
    stockGoNoBackup: "バックアップせず公式V15へ戻す",
    stockConfirmNoBackup: "バックアップを作成せず公式V15へ戻します。Melodeeの曲・音色・設定が失われる可能性があり、バックアップなしでは復元できません。続けますか？",
    stockRecovery: "更新中断の再開：全体バックアップはすでにパソコンへ保存済み",
    stockValid: "公式V15を確認しました", stockConfirm: "全体バックアップファイルをパソコンに保存できましたか？ 続けると公式V15へ戻します。Melodeeの曲は公式V15では開けません。",
    stockNeedBackup: "書き込みモードからはバックアップできません。保存済みバックアップがある場合は更新中断の再開にチェックするか、データ消失を了承してバックアップを省略してください。",
    stockUnsupported: "このMelodeeは全体バックアップに未対応です。先に最新版をインストールするか、データ消失を了承して「バックアップを省略する」を選び、もう一度押してください。",
    backup: "全体バックアップ保存中 ", stockError: "公式ファイルを確認できません: ",
    stockHead: "FM-1 が CHECK HEAD で公式ファイルを拒否しました（FAILED -5）。起動データが V15 と異なります。何も書き込んでおらず、Melodee はそのまま使えます。別の方法で戻そうとしないでください。上の FAILED -5 のリンクを読み、GitHub で報告してください。",
    stockHelp: "FM-1 に FAILED -5 と表示された場合（理由と報告方法）",
    loaderStock: "FM-1 は M-VAVE 公式の書き込みモードで待機しています。このモードには公式FM-1.fwscしか書き込めません。何も送っていません。先に「公式V15に戻す」を完了してください。",
    loaderMelodee: "FM-1 は Melodee の書き込みモードで待機しています。このモードには公式ファイルを送りません。何も送っていません。上の「インストール」で Melodee を完了してから、公式V15に戻してください。",
    loaderUnknown: "このインストーラーが知らない書き込みモードです。何も送っていません。この名前を添えて GitHub で報告してください: ",
    nomidi: "このブラウザでは使えません。Chrome か Edge で開いてください。", loadfail: "読み込めませんでした: ",
    start: "開始します", verify: "確認中", loader: "書き込みモードに切り替え中", write: "書き込み中",
    reboot: "再起動中", done: "完了しました", stopped: "途中で止まりました。USB を挿し直してもう一度押してください。",
    error: "エラー: ",
    denied: "MIDI（SysEx）の使用が許可されませんでした。アドレスバーのサイト設定で MIDI を許可して、もう一度押してください。",
    notfound: "FM-1 が見つかりません。USB ケーブル（データ通信用か）を確かめ、MIDI を使う他のアプリを閉じてから、もう一度押してください。",
    model: "接続されている機器は FM-1 ではありません。",
    lost: "USB の接続が切れました。挿し直してもう一度押してください。",
    noloader: "書き込みモードの FM-1 が見つかりません。USB を挿し直してもう一度押してください（終わるまで書き込みモードのままです）。",
    noreturn: "FM-1 が戻ってきません。電源を入れ直してください。",
    mismatch: "書き込みは終わりましたが、FM-1 が別のバージョンを報告しています: ",
    badpkg: "インストールするファイルが壊れています。ページを再読み込みしてください。",
    leave: "書き込み中です。ページを閉じないでください。",
    license: "GPL-3.0、無保証。ソース:",
  },
  en: {
    intro: "Connect the FM-1 by USB. (Works in Chrome or Edge.)",
    warn: "Do not unplug the USB cable while writing.",
    install: "Install", other: "日本語",
    releaseTitle: "Stable release", latest: "Latest",
    advancedTitle: "Advanced installs",
    rcTitle: "Release candidate", "rc-dual": "Release candidate",
    "rc-dualTag": "release candidate",
    rcWhy: "Preview of the next release, available for testing. Choose Standard to return to stable.",
    nightlyTitle: "Nightly (next)", "nightly-dual": "Nightly",
    "nightly-dualTag": "nightly",
    nightlyWhy: "Development builds from next. Save a complete backup in the editor before installing. Choose Standard to return to the release.",
    dual: "Standard", dualTag: "standard",
    editor: "Editor (after installing: edit sounds and sequences from the computer)",
    direct: "Connect the USB cable directly to the computer. No probe or Transporter is needed.",
    brick: "If an update is interrupted on firmware with boot protection, switch the FM-1 on again (or hold OCT− alone while switching on). Three short key-light flashes mean update mode: reconnect USB and press Install again. Older firmware and one long flash need the USB recovery instructions:",
    recoveryLink: "USB recovery instructions and downloads",
    beta: "Please report Melodee problems on GitHub. The author accepts no responsibility if your FM-1 is damaged or misbehaves because of this custom firmware. Use it at your own risk.",
    transitTitle: "Switching to another firmware",
    transitWhy: "Before leaving Melodee for another custom firmware, install TransitFM1. It is a temporary stop for switching firmware and makes no sound. For Baud Girl, install official V15 first, then use Baud Girl’s own installer.",
    transitFirst: "Install TransitFM1 0.1.3 using the button below. Do not use the old 0.1 or withdrawn 0.1.2.",
    transitNext: "Once the FM-1 shows TRANSIT, open the next firmware’s own installer and install it there.",
    transitGo: "Back up and install TransitFM1",
    transitGoNoBackup: "Install TransitFM1 without backup",
    transitConfirm: "Has the complete backup file been saved to this computer? Continue to install TransitFM1, then use the next firmware’s own installer.",
    transitConfirmNoBackup: "Install TransitFM1 without creating a backup? Melodee music, sounds and settings may be lost and cannot be restored without a backup. Continue?",
    transitLoading: "Downloading and verifying TransitFM1",
    transitDone: "TransitFM1 installed. Check that the FM-1 shows TRANSIT, then open the next firmware’s own installer.",
    transitSafety: "TransitFM1 guards the next firmware’s write against interruptions, except possibly its last sector. Baud Girl and other images beyond its write range require official V15 first. Charge the FM-1, close DAWs and other apps sending MIDI, and keep USB connected while writing. If interrupted, resume with the same firmware.",
    transitAbout: "TransitFM1 instructions and recovery", transitSource: "Source and licences by Quixotic7",
    back: "Get official FM-1 V15 from M-VAVE and select the unmodified FM-1.fwsc. The browser validates it locally; it is not sent to a server. The file is installed unchanged, with M-VAVE's own update loader.",
    stockTitle: "Return to official V15", stockFile: "Official FM-1.fwsc", stockGo: "Back up and return to official V15",
    stockBackup: "Current music, saved music, sound banks, samples and settings are saved first. If backups are unavailable, update Melodee or choose to skip the backup and accept possible data loss. To restore an archive later, reinstall Melodee and use the editor.",
    stockSkipBackup: "Skip backup (Melodee music, sounds and settings may be lost)",
    stockGoNoBackup: "Return to official V15 without backup",
    stockConfirmNoBackup: "Return to official V15 without creating a backup? Melodee music, sounds and settings may be lost and cannot be restored without a backup. Continue?",
    stockRecovery: "Resume an interrupted update: I already saved a complete backup to this computer",
    stockValid: "Official V15 verified", stockConfirm: "Has the complete backup file been saved to this computer? Continue to return to official V15. Official V15 cannot open Melodee music.",
    stockNeedBackup: "An update loader cannot export a backup. Select resume if you already saved a complete backup, or choose Skip backup to continue and accept possible data loss.",
    stockUnsupported: "This Melodee does not support complete backups. Install the current Melodee first, or select Skip backup to accept possible data loss and try again.",
    backup: "Saving complete backup ", stockError: "Cannot validate official firmware: ",
    stockHead: "The FM-1 refused the official file at CHECK HEAD (FAILED -5): its boot data differs from V15's. Nothing was written; Melodee stays installed. Do not try other ways to return to stock. Open the FAILED -5 link above and report it on GitHub.",
    stockHelp: "If the FM-1 shows FAILED -5: why, and how to report it",
    loaderStock: "The FM-1 is waiting in M-VAVE's official update mode, which only accepts the official FM-1.fwsc. Nothing was sent. Finish Return to official V15 first.",
    loaderMelodee: "The FM-1 is waiting in Melodee's update mode, which never receives the official file. Nothing was sent. Press Install above to finish Melodee first, then return to official V15.",
    loaderUnknown: "The FM-1 is in an update mode this installer does not know. Nothing was sent. Please report it on GitHub with this name: ",
    nomidi: "This browser cannot do it. Open the page in Chrome or Edge.", loadfail: "Could not load: ",
    start: "Starting", verify: "Verifying", loader: "Switching to update mode", write: "Writing",
    reboot: "Restarting", done: "Done", stopped: "Stopped. Replug the USB cable and press Install again.",
    error: "Error: ",
    denied: "MIDI (SysEx) access was not allowed. Allow MIDI in the site settings (address bar) and press Install again.",
    notfound: "FM-1 not found. Check the USB cable (a data cable), close other apps that use MIDI, and press Install again.",
    model: "The connected device is not an FM-1.",
    lost: "The USB connection was lost. Replug it and press Install again.",
    noloader: "The FM-1 in update mode was not found. Replug the USB cable and press Install again (it stays in update mode until it is finished).",
    noreturn: "The FM-1 did not come back. Power-cycle it.",
    mismatch: "Written, but the FM-1 reports another version: ",
    badpkg: "The file to install is damaged. Reload the page.",
    leave: "Writing. Do not close the page.",
    license: "GPL-3.0, no warranty. Source:",
  },
};
Object.assign(TEXT.en, {"kicker": "Firmware for the M-VAVE FM-1", "installTitle": "Install Melodee", "factTime": "About 3 minutes", "connectTitle": "Connect the FM-1", "connectHelp": "Switch it on, close music apps and other editor tabs, and allow MIDI access when asked.", "find": "Check connection", "chooseTitle": "Choose your version", "backupFirst": "Back up your music and sounds in the editor before updating.", "beforeStart": "Before you start", "stayHere": "Keep this tab open and in front", "stayWhy": "Stay on this page until it says Done.", "keepPower": "Keep the FM-1 connected and powered", "keepWhy": "Keep the cable connected. Plug your laptop into power.", "screenNormal": "UPDATING or a dark screen is normal during installation.", "backupAck": "I've saved a backup, or have nothing to keep.", "cancel": "Cancel", "startInstall": "Start installation", "runWarning": "Keep this tab open and in front. Don't unplug the FM-1.", "prepare": "Prepare", "stall": "Waiting for the FM-1. Keep the cable connected.", "resumeInstall": "Resume installation", "close": "Close", "details": "Details", "pendingInstall": "An installation did not finish. Select the same version and resume:", "differentPending": "Resume using the same selected build as the interrupted installation:", "busy": "Another tab is using the installer. Finish or close it there first.", "comeBack": "Come back to the installer", "installingTitle": "Installing Melodee", "etaSoon": "Less than a minute left", "etaMinutes": "minutes left", "backgroundKept": "The installation continued while you were away. Please stay here until it finishes.", "backgroundSlow": "The browser slowed this page while you were away. It is continuing; please stay here.", "end_done": "Installation complete", "end_safe": "Nothing was written", "end_paused": "Installation paused", "end_restart": "The FM-1 did not return as expected", "installedVerified": "The FM-1 restarted and reported the expected firmware.", "tip_done": "You can open the editor or unplug the FM-1.", "tip_safe": "Check the connection and other music apps, then try again.", "tip_paused": "Reconnect the FM-1 and resume the same build. The write starts again from the beginning.", "tip_restart": "Power-cycle the FM-1 and check its screen. If it stays in update mode, reconnect and resume the same build.", "badHash": "The firmware checksum does not match the published build. Reload the page."});
Object.assign(TEXT.ja, {"kicker": "M-VAVE FM-1 用ファームウェア", "installTitle": "Melodee をインストール", "factTime": "約3分", "connectTitle": "FM-1 を接続", "connectHelp": "電源を入れ、音楽アプリや別のエディタタブを閉じてください。MIDI の許可を求められたら許可してください。", "find": "接続を確認", "chooseTitle": "バージョンを選ぶ", "backupFirst": "更新前にエディタで曲と音色をバックアップしてください。", "beforeStart": "開始前に確認", "stayHere": "このタブを開いたまま前面に保つ", "stayWhy": "完了と表示されるまで、このページから離れないでください。", "keepPower": "FM-1 の接続と電源を保つ", "keepWhy": "ケーブルを抜かず、パソコンも電源につないでください。", "screenNormal": "更新中は UPDATING 表示や画面の消灯が正常です。", "backupAck": "バックアップを保存済み、または保存するデータはありません。", "cancel": "キャンセル", "startInstall": "インストール開始", "runWarning": "このタブを前面に保ち、FM-1 を抜かないでください。", "prepare": "準備", "stall": "FM-1 の応答を待っています。接続を保ってください。", "resumeInstall": "インストールを再開", "close": "閉じる", "details": "詳細", "pendingInstall": "未完了のインストールがあります。同じバージョンを選んで再開してください：", "differentPending": "中断したインストールと同じビルドを選んで再開してください：", "busy": "別のタブでインストール中です。そのタブで完了するか閉じてください。", "comeBack": "インストーラに戻ってください", "installingTitle": "Melodee インストール中", "etaSoon": "残り1分未満", "etaMinutes": "分（残り）", "backgroundKept": "タブを離れた間も処理を続けました。完了までこのタブに留まってください。", "backgroundSlow": "タブを離れた間にブラウザが処理を遅くしました。再開しています。このタブに留まってください。", "end_done": "インストール完了", "end_safe": "書き込みは行われませんでした", "end_paused": "インストールを中断", "end_restart": "FM-1 の再起動を確認できませんでした", "installedVerified": "FM-1 の再起動と、指定したファームウェアの識別情報を確認しました。", "tip_done": "エディタを開くか、FM-1 を取り外せます。", "tip_safe": "接続と音楽アプリを確認して、もう一度お試しください。", "tip_paused": "FM-1 を再接続し、同じビルドで再開してください。書き込みは最初から始まります。", "tip_restart": "FM-1 の電源を入れ直して画面を確認してください。更新モードのままなら、再接続して同じビルドで再開してください。", "badHash": "ファームウェアのチェックサムが一致しません。ページを再読み込みしてください。"});

const t=k=>TEXT[getLanguage()][k] || k;
const session=createInstallSession({t});
let writing=false, stock=null, stockFileEpoch=0;
function applyLang() {
 document.querySelectorAll('[data-leave-t]').forEach(e=>{e.textContent=t(e.dataset.leaveT);});
 $('stock-go').textContent=t($('stock-skip-backup').checked?'stockGoNoBackup':'stockGo');
 $('transit-go').textContent=t($('transit-skip-backup').checked?'transitGoNoBackup':'transitGo');
}
$('stock-skip-backup').addEventListener('change',applyLang);
$('transit-skip-backup').addEventListener('change',applyLang);
const sayK=(k,arg='')=>{$('status').textContent=t(k)+arg;session.message(t(k)+arg);};
function lock(on) {
 writing=on;
 $('stock-go').disabled=on||!stock;
 ['stock-file','stock-recovery','stock-skip-backup','transit-go','transit-recovery','transit-skip-backup'].forEach(id=>{$(id).disabled=on;});
}
const lockInstaller=on=>{setBusy(on);lock(on);};
// the official loader of each SMK-37 model ("ota-SMK-37 Pro_016") is official too; a Melodee package's own loader
// carries its model (fm1ota.js melodeeLoader)
const SMK_STOCK_LOADER=/^ota-(SMK-37 Pro|SMK-37 Elite|MKE-P37)_016$/;
const refuseLoader=id=>sayK(id===STOCK_LOADER||SMK_STOCK_LOADER.test(id)?'loaderStock':id===melodeeLoader(id.slice(4))?'loaderMelodee':'loaderUnknown',id);
$("stock-file").addEventListener("change", async () => {
  if (writing) return;
  const epoch = ++stockFileEpoch, file = $("stock-file").files[0]; stock = null; lockInstaller(false);
  if (!file) return;
  try {
    if (file.size !== STOCK_V15_SIZE && file.size !== STOCK_SMK016_SIZE) throw new Error("FM-1 V15 / FM-1.fwsc, SMK-37 Pro V016 / SMK-37_Pro_016.fwsc");
    const verified = await validateStockPackage(new Uint8Array(await file.arrayBuffer()));
    if (epoch !== stockFileEpoch) return;
    stock = verified; if (!writing) sayK("stockValid"); lockInstaller(writing);
  } catch (e) { if (epoch === stockFileEpoch && !writing) sayK("stockError", e.message); }
});
function saveArchive(archive) {
  const url = URL.createObjectURL(new Blob([JSON.stringify(archive)], {type:"application/json"}));
  const a = document.createElement("a"); a.href = url; a.download = `Melodee-backup-${new Date().toISOString().slice(0,10)}.json`;
  document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function archiveInfo(a) {
  let pos = a.indexOf(0); if (pos < 0) throw new Error("Invalid INFO");
  const version = String.fromCharCode(...a.slice(0,pos)); pos++;
  const engines = a[pos]; pos += 5;
  for (let i = 0; i < engines; i++) { const end = a.indexOf(0,pos); if (end < 0) throw new Error("Invalid INFO"); pos = end + 1; }
  const ext = a.slice(pos + 1);
  // Archives contain opaque objects; backup support is independent of motion and UI versions.
  const supported = /^(MELODEE|FELUCCA) /i.test(version) &&
    ext[8] === 0x42 && ext[9] === 1 && (ext[10] & 1);
  return {version,supported};
}
async function leaveMelodee(target) {
  if (isBusy() || (target === "stock" && !stock)) return;
  if (!navigator.requestMIDIAccess) { sayK("nomidi"); return; }
  let selected = stock;
  // the official file goes to its model's official loader (FM-1: STOCK_LOADER; SMK-37 Pro: "ota-SMK-37 Pro_016")
  const transit = target === "transit", ownLoader = transit ? MELODEE_LOADER : "ota-" + stock.product;
  const model = transit ? "FM-1" : stock.product.split("_")[0];
  const skipBackup = $(target + "-skip-backup").checked;
  const confirmation = () => confirm(t(target + (skipBackup ? "ConfirmNoBackup" : "Confirm")));
  let completed = false, failure = null;
  const complete = () => { completed = true; $("bar").value = 100; sayK(transit ? "transitDone" : "done"); };
  lockInstaller(true); $("bar").value = 0;
  let link = null;
  try {
    if (transit) { sayK("transitLoading"); selected = await fetchTransitPackage(); }
    const key = transit ? "transit:0.1.3" : stock.product === "FM-1_015" ? "stock:V15" : "stock:" + stock.product;
    if (!await session.begin({key,label:transit ? "TransitFM1 0.1.3" : stock.product === "FM-1_015" ? "M-VAVE V15" : "M-VAVE " + stock.product,image:selected.image,onResume:() => leaveMelodee(target)})) return;
    let midi;
    try { midi = await navigator.requestMIDIAccess({sysex:true}); } catch (e) { sayK("denied"); return; }
    const up = new SteadyUpdater(midi, log);
    const step = (k, n, bytes) => {
      session.step(k,n,bytes);
      log(`${k} ${n ?? ""}`);
      if (["start","verify","loader","write","reboot","done"].includes(k)) sayK(k);
      if (k === "verify") $("bar").value = Math.min(10, n / 10);
      if (k === "write") $("bar").value = Math.min(99, 10 + n / (selected.image.length / 512) * 90);
    };
    const loader = await up.find(id => /^ota-/i.test(id.model));
    if (loader) {
      loader.link.close();
      if (loader.id.text !== ownLoader) { refuseLoader(loader.id.text); return; }
      if (!skipBackup && !$(target + "-recovery").checked) { sayK("stockNeedBackup"); return; }
      if (!confirmation()) return;
      session.resume();
      if (await up.resume(selected.image, step, selected.product, ownLoader)) complete();
      else sayK("stopped");
    } else {
      const device = await up.find(id => id.model === model);
      if (!device) { sayK("notfound"); return; }
      const input = device.link.input, output = device.link.output; device.link.close();
      if (!skipBackup) {
        link = new BackupConnection(input, output);
        const info = archiveInfo(await link.request([1,[]]));
        if (!info.supported) { sayK("stockUnsupported"); return; }
        const archive = await captureBackup((r,o) => link.request(r,o), info.version,
          (done,total) => sayK("backup", `${Math.round(done / Math.max(1,total) * 100)}%`));
        saveArchive(archive); link.close(); link = null;
      }
      if (!confirmation()) return;
      await up.install(selected.image, selected.product, step, ownLoader);
      complete();
    }
  } catch (e) {
    // Melodee stops reading at CHECK HEAD when the device's boot data differs from V15's (FAILED -5):
    // refused, never retried another way
    if (!transit && e.code === "stopped" && e.lastRead >= selected.head.from && e.lastRead < selected.head.to) sayK("stockHead");
    else if (e.code === "wrongloader") refuseLoader(e.detail);
    else if (e.code && TEXT[getLanguage()][e.code]) sayK(e.code,e.detail || ""); else sayK("error",e.message);
    failure = {code:e.code,message:$("status").textContent};
    log(String(e.stack || e));
  } finally { if (link) link.close(); try { session.finish(completed,failure || new Error($("status").textContent)); } finally { lockInstaller(false); } }
}
$("stock-go").addEventListener("click", () => leaveMelodee("stock"));
$("transit-go").addEventListener("click", () => leaveMelodee("transit"));

return {lock,applyLang};
}
