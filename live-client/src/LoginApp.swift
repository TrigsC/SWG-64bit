import AppKit
import Foundation
import Darwin

func credentialFrame(username: String, password: String) throws -> Data {
    let user=Data(username.utf8),secret=Data(password.utf8)
    guard !user.isEmpty,!secret.isEmpty,user.count<=4096,secret.count<=4096,
          !user.contains(0),!secret.contains(0) else {throw NSError(domain:"Login",code:1,userInfo:[NSLocalizedDescriptionKey:"Enter an account name and password (up to 4096 bytes each)."])}
    var result=Data()
    for size in [user.count,secret.count] {
        let value=UInt32(size);result.append(contentsOf:[UInt8((value>>24)&255),UInt8((value>>16)&255),UInt8((value>>8)&255),UInt8(value&255)])
    }
    result.append(user);result.append(secret);return result
}
func serverSettings(root: URL) -> (String,String) {
    guard let config=try? String(contentsOf:root.appendingPathComponent("swgemu_login.cfg"),encoding:.utf8) else {return ("","44453")}
    var host="",port="44453"
    for line in config.components(separatedBy:.newlines) {
        let parts=line.split(separator:"=",maxSplits:1).map{String($0).trimmingCharacters(in:.whitespaces)}
        if parts.count==2 {
            if parts[0]=="loginServerAddress0" {host=parts[1]}
            if parts[0]=="loginServerPort0" {port=parts[1]}
        }
    }
    return (host,port)
}

final class LoginApp: NSObject, NSApplicationDelegate {
    let root=Bundle.main.bundleURL.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    var window:NSWindow!
    let host=NSTextField(),port=NSTextField(),username=NSTextField(),password=NSSecureTextField(),character=NSTextField()
    let status=NSTextField(wrappingLabelWithString:"Enter your existing SWGEmu account, then connect.")
    let connect=NSButton(title:"Connect",target:nil,action:nil)
    let probe=NSButton(title:"Check server",target:nil,action:nil)
    let stop=NSButton(title:"Disconnect",target:nil,action:nil)
    let movementTest=NSButton(title:"Test movement",target:nil,action:nil)
    let spinner=NSProgressIndicator()
    let details=NSTextView()
    var process:Process?,generation=0,pending=Data(),worldReady=false,canceled=false,quitting=false,checking=false

    func applicationDidFinishLaunching(_ notification:Notification) {
        NSApp.setActivationPolicy(.regular)
        let settings=serverSettings(root:root);host.stringValue=settings.0;port.stringValue=settings.1
        character.placeholderString="First existing character"
        window=NSWindow(contentRect:NSRect(x:0,y:0,width:620,height:700),styleMask:[.titled,.closable,.miniaturizable],backing:.buffered,defer:false)
        window.title="SWG Native Client";window.center();window.isReleasedWhenClosed=false
        let title=NSTextField(labelWithString:"Star Wars Galaxies")
        title.font = .systemFont(ofSize:25,weight:.semibold)
        let subtitle=NSTextField(wrappingLabelWithString:"Native Mac client • SWGEmu / Pre-CU\nConnect to your configured server and load an existing character.")
        subtitle.textColor = .secondaryLabelColor
        func row(_ name:String,_ field:NSTextField)->NSStackView {
            let label=NSTextField(labelWithString:name);label.widthAnchor.constraint(equalToConstant:96).isActive=true
            field.heightAnchor.constraint(equalToConstant:28).isActive=true
            let row=NSStackView(views:[label,field]);row.orientation = .horizontal;row.alignment = .centerY;return row
        }
        let fields=[row("Server",host),row("Port",port),row("Account",username),row("Password",password),row("Character",character)]
        connect.target=self;connect.action=#selector(startLogin);connect.keyEquivalent="\r"
        probe.target=self;probe.action=#selector(checkServer)
        stop.target=self;stop.action=#selector(disconnect);stop.isEnabled=false
        movementTest.target=self;movementTest.action=#selector(testMovement);movementTest.isEnabled=false
        let buttons=NSStackView(views:[probe,connect,stop,movementTest]);buttons.orientation = .horizontal;buttons.spacing=10
        spinner.style = .spinning;spinner.controlSize = .small;spinner.isDisplayedWhenStopped=false
        let statusRow=NSStackView(views:[spinner,status]);statusRow.orientation = .horizontal;statusRow.alignment = .top;statusRow.spacing=10
        status.heightAnchor.constraint(greaterThanOrEqualToConstant:46).isActive=true
        let note=NSTextField(wrappingLabelWithString:"Passwords are passed through a private process pipe and are not saved.\nWASD move · R walk/run · E action · C stop action · M ambient · drag orbit · wheel zoom · Tab targets · Q point · Backspace clear · Esc leave world\nChat: Enter opens chat and submits · Esc returns to world · /stand, /kneel, /sitServer")
        note.font = .systemFont(ofSize:11);note.textColor = .secondaryLabelColor
        let scroll=NSScrollView();scroll.hasVerticalScroller=true;scroll.borderType = .bezelBorder
        details.isEditable=false;details.isSelectable=true;details.font = .monospacedSystemFont(ofSize:11,weight:.regular)
        details.textContainerInset=NSSize(width:8,height:8);scroll.documentView=details
        scroll.heightAnchor.constraint(equalToConstant:116).isActive=true
        let stack=NSStackView(views:[title,subtitle]+fields+[buttons,statusRow,note,scroll]);stack.orientation = .vertical;stack.alignment = .leading;stack.spacing=12
        stack.translatesAutoresizingMaskIntoConstraints=false
        window.contentView!.addSubview(stack)
        NSLayoutConstraint.activate([stack.leadingAnchor.constraint(equalTo:window.contentView!.leadingAnchor,constant:26),stack.trailingAnchor.constraint(equalTo:window.contentView!.trailingAnchor,constant:-26),stack.topAnchor.constraint(equalTo:window.contentView!.topAnchor,constant:24)])
        for view in fields+[statusRow] {view.widthAnchor.constraint(equalTo:stack.widthAnchor).isActive=true}
        scroll.widthAnchor.constraint(equalTo:stack.widthAnchor).isActive=true
        window.makeKeyAndOrderFront(nil);window.makeFirstResponder(username);NSApp.activate(ignoringOtherApps:true)
        if CommandLine.arguments.contains("--ui-check-server") {checkServer()}
    }
    func setBusy(_ value:Bool) {
        connect.isEnabled = !value;probe.isEnabled = !value;stop.isEnabled=value;movementTest.isEnabled=value&&worldReady
        for field in [host,port,username,password,character] {field.isEnabled = !value}
        if value {spinner.startAnimation(nil)}else{spinner.stopAnimation(nil)}
    }
    func message(_ value:String) {
        status.stringValue=value
        details.string += value+"\n"
        if details.string.count>18000 {details.string=String(details.string.suffix(12000))}
        details.scrollToEndOfDocument(nil)
    }
    @objc func checkServer(){launch(checkOnly:true)}
    @objc func startLogin(){launch(checkOnly:false)}
    func launch(checkOnly:Bool) {
        guard process==nil else{return}
        let address=host.stringValue.trimmingCharacters(in:.whitespacesAndNewlines)
        guard !address.isEmpty,let number=UInt16(port.stringValue),number>0 else {message("Enter a server address and a port between 1 and 65535.");return}
        var frame=Data()
        if !checkOnly {
            do {frame=try credentialFrame(username:username.stringValue,password:password.stringValue)}catch{message(error.localizedDescription);return}
        }
        let child=Process(),output=Pipe(),input=Pipe()
        child.executableURL=Bundle.main.bundleURL.appendingPathComponent("Contents/Helpers/SWG Native World.app/Contents/MacOS/SWGNativeWorld")
        child.currentDirectoryURL=root
        var args=["--host",address,"--port",String(number)]
        if checkOnly {args += ["--connection-check"]}
        else {
            args += ["--credentials-stdin","--native-client","--client-path",root.path]
            if !character.stringValue.isEmpty {args += ["--character-name",character.stringValue]}
        }
        child.arguments=args;child.standardInput=input;child.standardOutput=output;child.standardError=output
        var environment=ProcessInfo.processInfo.environment
        environment["VK_DRIVER_FILES"]=root.appendingPathComponent("modernization/tools/vulkan/moltenvk/MoltenVK/MoltenVK/dynamic/dylib/macOS/MoltenVK_icd.json").path
        child.environment=environment
        generation+=1;let current=generation;pending.removeAll();worldReady=false;canceled=false;checking=checkOnly
        output.fileHandleForReading.readabilityHandler={ [weak self] handle in
            let data=handle.availableData
            if data.isEmpty {handle.readabilityHandler=nil;return}
            DispatchQueue.main.async {guard let self=self,self.generation==current else{return};self.consume(data)}
        }
        child.terminationHandler={ [weak self] result in
            DispatchQueue.main.async {
                guard let self=self,self.generation==current else{return}
                output.fileHandleForReading.readabilityHandler=nil
                let rest=output.fileHandleForReading.readDataToEndOfFile();if !rest.isEmpty {self.consume(rest)}
                if !self.pending.isEmpty {self.line(String(decoding:self.pending,as:UTF8.self));self.pending.removeAll()}
                self.process=nil;self.setBusy(false)
                if self.canceled {self.message("Disconnected.")}
                else if result.terminationStatus==0 {self.message(checkOnly ? "Server answered the SWG handshake. Ready to log in." : "World session closed. You can connect again.")}
                else {self.message("Connection ended. See the connection details above and try again.")}
                if self.quitting {NSApp.reply(toApplicationShouldTerminate:true)}
            }
        }
        do {
            try child.run();process=child;setBusy(true);message(checkOnly ? "Checking server…" : "Connecting…")
            if !checkOnly {input.fileHandleForWriting.write(frame);password.stringValue="";frame.resetBytes(in:0..<frame.count)}
            try? input.fileHandleForWriting.close()
            DispatchQueue.main.asyncAfter(deadline:.now()+95) { [weak self,weak child] in
                guard let self=self,self.generation==current,!self.worldReady,let child=child,child.isRunning else{return}
                self.message("Connection did not finish in time.");self.disconnect()
            }
        }catch {
            output.fileHandleForReading.readabilityHandler=nil;password.stringValue="";process=nil;setBusy(false);message("Could not start the client: "+error.localizedDescription)
        }
    }
    func consume(_ data:Data) {
        pending.append(data)
        while let end=pending.firstIndex(of:10) {
            let text=String(decoding:pending[..<end],as:UTF8.self);pending.removeSubrange(...end);line(text)
        }
        if pending.count>65536 {pending.removeAll()}
    }
    func line(_ raw:String) {
        let text=String(raw.prefix(600))
        if text.hasPrefix("NATIVE_WORLD_READY") {worldReady=true;movementTest.isEnabled=true;spinner.stopAnimation(nil);message("Connected — the world is ready.")}
        else if text.hasPrefix("NATIVE_FRAME_JOURNAL") {message("Coordinate diagnostics: "+String(text.dropFirst("NATIVE_FRAME_JOURNAL ".count)))}
        else if text.hasPrefix("NATIVE_CELL_JOURNAL") {message("Cell diagnostics: "+String(text.dropFirst("NATIVE_CELL_JOURNAL ".count)))}
        else if text.hasPrefix("NATIVE_FLOOR_JOURNAL") {message("Floor diagnostics: "+String(text.dropFirst("NATIVE_FLOOR_JOURNAL ".count)))}
        else if text.hasPrefix("NATIVE_CAMERA_JOURNAL") {message("Camera diagnostics: "+String(text.dropFirst("NATIVE_CAMERA_JOURNAL ".count)))}
        else if text.hasPrefix("NATIVE_CAMERA_TEST") {message(text.replacingOccurrences(of:"NATIVE_CAMERA_TEST",with:"Camera check:"))}
        else if text.hasPrefix("NATIVE_MOVEMENT_JOURNAL") {message("Movement diagnostics: "+String(text.dropFirst("NATIVE_MOVEMENT_JOURNAL ".count)))}
        else if text.hasPrefix("NATIVE_MOVEMENT_TEST started") {movementTest.isEnabled=false;message("Movement test: walking out and back, then checking idle synchronization (36 seconds).")}
        else if text.hasPrefix("NATIVE_MOVEMENT_TEST complete") {movementTest.isEnabled=worldReady;message("Movement test finished. Diagnostics saved locally.")}
        else if text.hasPrefix("NATIVE_ANIMATION_READY") {message("Character animations ready.")}
        else if text.contains("Milestone 1 complete") {message("Account authenticated. Selecting character…")}
        else if text.contains("Selecting character") {message(text.trimmingCharacters(in:.whitespaces))}
        else if text.hasPrefix("Connecting to zone server") {message("Connecting to the galaxy…")}
        else if text.hasPrefix("SOE handshake complete") {!checking ? message("Server connected. Authenticating account…") : message("Server connected.")}
        else if text.hasPrefix("Scene ready") {message("Zone connected. Loading graphics and assets…")}
        else if text.hasPrefix("Error:") || text.hasPrefix("Timeout:") || text.hasPrefix("Disconnected:") || text.hasPrefix("Server ErrorMessage:") || text.hasPrefix("[NETWORK]") {message(text)}
    }
    @objc func testMovement() {
        guard worldReady,let child=process,child.isRunning else{return}
        if Darwin.kill(child.processIdentifier,SIGUSR1) != 0 {message("Could not start movement test.")}
    }
    @objc func disconnect() {
        guard let child=process,child.isRunning else{return};canceled=true;child.terminate();message("Disconnecting…")
    }
    func applicationShouldTerminate(_ sender:NSApplication)->NSApplication.TerminateReply {
        password.stringValue=""
        if process != nil {quitting=true;disconnect();return .terminateLater}
        return .terminateNow
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender:NSApplication)->Bool{return true}
}
if CommandLine.arguments.contains("--credential-fixture") {
    let frame=try credentialFrame(username:"fixture-user",password:"test-$-\"-\n-secret")
    FileHandle.standardOutput.write(frame)
} else {
    let app=NSApplication.shared,delegate=LoginApp();app.delegate=delegate;app.run()
}
