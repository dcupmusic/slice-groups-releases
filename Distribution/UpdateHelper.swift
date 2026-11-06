import Foundation
import Security
import CryptoKit
import Darwin

struct Part: Decodable {let offset:Int;let size:Int;let sha256:String}
struct Entry: Decodable {let path:String;let size:Int;let sha256:String;let mode:Int;let strategy:String;let parts:[Part]?;let prefix:Int?;let bodyEnd:Int?;let bodySha256:String?;let prefixParts:[Part]?;let tailParts:[Part]?}
struct BundleSpec:Decodable {let role:String;let name:String;let files:[Entry]}
struct Manifest:Decodable {let format:Int;let version:String;let team:String;let bundles:[BundleSpec]}
struct UpdateError:Error,CustomStringConvertible {let description:String;init(_ message:String){description=message}}
let fm=FileManager.default
func digest(_ data:Data)->String {SHA256.hash(data:data).map{String(format:"%02x",$0)}.joined()}
func regular(_ path:URL)->Bool {guard let values=try? path.resourceValues(forKeys:[.isRegularFileKey,.isSymbolicLinkKey]) else{return false};return values.isRegularFile==true && values.isSymbolicLink != true}
func validRelative(_ path:String)->Bool {!path.isEmpty && !path.hasPrefix("/") && !path.split(separator:"/",omittingEmptySubsequences:false).contains(where:{$0.isEmpty || $0=="." || $0==".."})}
func fileHash(_ path:URL)throws->String {guard regular(path) else{throw UpdateError("Not a regular file: \(path.lastPathComponent)")};return digest(try Data(contentsOf:path,options:.mappedIfSafe))}
func verify(_ bundle:URL)throws {
 var code:SecStaticCode?;guard SecStaticCodeCreateWithPath(bundle as CFURL,[],&code)==errSecSuccess,let code=code else{throw UpdateError("Cannot read reconstructed code signature")}
 var requirement:SecRequirement?;let rule="anchor apple generic and certificate leaf[subject.OU] = \"Q5YNM8U8JB\" and identifier \"audio.slicegroups.plugin\""
 guard SecRequirementCreateWithString(rule as CFString,[],&requirement)==errSecSuccess else{throw UpdateError("Invalid developer requirement")}
 var error:Unmanaged<CFError>?;let flags=SecCSFlags(rawValue:kSecCSCheckNestedCode|kSecCSStrictValidate)
 guard SecStaticCodeCheckValidityWithErrors(code,flags,requirement,&error)==errSecSuccess else{throw UpdateError("Reconstructed app signature failed: \(error?.takeRetainedValue().localizedDescription ?? "unknown error")")}
}
func command(_ name:String,_ arguments:[String])->Int32 {let p=Process();p.executableURL=URL(fileURLWithPath:name);p.arguments=arguments;p.standardOutput=FileHandle.nullDevice;p.standardError=FileHandle.nullDevice;do{try p.run();p.waitUntilExit();return p.terminationStatus}catch{return -1}}
func requireClosed()throws {
 #if !UPDATE_HELPER_TEST
 for host in ["Live","Slice Groups","REAPER","Logic Pro","GarageBand","BitwigStudio","Cubase","Nuendo","Studio One"] {if command("/usr/bin/pgrep",["-x",host])==0{throw UpdateError("Close \(host) before installing this update.")}}
 #endif
}
func copyClone(_ source:URL,_ target:URL)throws {
 let result=source.withUnsafeFileSystemRepresentation {s in target.withUnsafeFileSystemRepresentation {t in copyfile(s,t,nil,copyfile_flags_t(COPYFILE_ALL|COPYFILE_CLONE))}}
 if result != 0 {try? fm.removeItem(at:target);try fm.copyItem(at:source,to:target)}
}
final class Updater {
 let manifest:Manifest;let data:Data;let base:Int;let root:URL;let home:URL?;var sources:[URL]=[];var reused:[String:URL]=[:];var verifiedParts=Set<String>()
 init(_ file:URL,_ volume:String)throws {
  data=try Data(contentsOf:file,options:.mappedIfSafe);guard data.count>=12,Data(data.prefix(8))==Data("SGUPD001".utf8) else{throw UpdateError("Invalid update payload")}
  let length=data[8..<12].enumerated().reduce(0){$0 | (Int($1.element)<<($1.offset*8))};guard length>0,length<=16*1024*1024,length<=data.count-12 else{throw UpdateError("Invalid update manifest length")};base=12+length
  manifest=try JSONDecoder().decode(Manifest.self,from:data.subdata(in:12..<base));guard manifest.format==1,manifest.team=="Q5YNM8U8JB",manifest.bundles.count==2,Set(manifest.bundles.map{$0.role})==Set(["plugin","standalone"]) else{throw UpdateError("Unsupported update manifest")}
  #if UPDATE_HELPER_TEST
  root=URL(fileURLWithPath:volume,isDirectory:true);home=root.appendingPathComponent("Users/testuser",isDirectory:true)
  #else
  guard volume=="/" else{throw UpdateError("Install this update on the current startup disk.")};root=URL(fileURLWithPath:"/",isDirectory:true)
  if let user=(try? fm.attributesOfItem(atPath:"/dev/console")[.ownerAccountName]) as? String,user != "root",user != "loginwindow",let path=NSHomeDirectoryForUser(user){home=URL(fileURLWithPath:path,isDirectory:true)}else{home=nil}
  #endif
  sources=[root.appendingPathComponent("Library/Audio/Plug-Ins/VST3/Slice Groups.vst3"),root.appendingPathComponent("Applications/Slice Groups.app")]
  if let home=home{sources.append(home.appendingPathComponent("Library/Audio/Plug-Ins/VST3/Slice Groups.vst3"))}
  sources=sources.filter{fm.fileExists(atPath:$0.path)}
  for bundle in manifest.bundles {guard (bundle.role=="plugin" && bundle.name=="Slice Groups.vst3") || (bundle.role=="standalone" && bundle.name=="Slice Groups.app") else{throw UpdateError("Invalid bundle target")};guard bundle.files.count<20000 else{throw UpdateError("Too many update files")};var paths=Set<String>();for entry in bundle.files {guard validRelative(entry.path),entry.path.hasPrefix("Contents/"),paths.insert(entry.path).inserted,entry.size>=0,entry.size<2_147_483_647 else{throw UpdateError("Invalid update file")}}}
 }
 func destination(_ role:String)throws->URL {
  if role=="standalone" {return root.appendingPathComponent("Applications/Slice Groups.app")}
  let system=root.appendingPathComponent("Library/Audio/Plug-Ins/VST3/Slice Groups.vst3");let personal=home?.appendingPathComponent("Library/Audio/Plug-Ins/VST3/Slice Groups.vst3")
  if fm.fileExists(atPath:system.path),let personal=personal,fm.fileExists(atPath:personal.path){throw UpdateError("Two VST3 copies were found. Remove the duplicate before updating.")}
  if let personal=personal,fm.fileExists(atPath:personal.path){return personal};return system
 }
 func checkDestination(_ target:URL)throws {
  guard fm.fileExists(atPath:target.path) else{return}
  let values=try target.resourceValues(forKeys:[.isSymbolicLinkKey]);guard values.isSymbolicLink != true else{throw UpdateError("The installed app is a symbolic link. Use the full installer.")}
  let info=NSDictionary(contentsOf:target.appendingPathComponent("Contents/Info.plist"))
  guard info?["CFBundleIdentifier"] as? String == "audio.slicegroups.plugin" else{throw UpdateError("The destination is not a recognised Slice Groups installation.")}
 }
 func blob(_ parts:[Part])throws->Data {var result=Data();for p in parts{guard p.offset>=0,p.size>=0,p.size<=65536,result.count<=64*1024*1024-p.size,p.offset<=data.count-base,p.size<=data.count-base-p.offset else{throw UpdateError("Invalid payload range")};let block=data.subdata(in:(base+p.offset)..<(base+p.offset+p.size));if !verifiedParts.contains(p.sha256){guard digest(block)==p.sha256 else{throw UpdateError("Update payload checksum failed")};verifiedParts.insert(p.sha256)};result.append(block)};return result}
 func reuse(_ entry:Entry)throws->URL {
  let key=entry.strategy+":"+entry.sha256;if let found=reused[key]{return found}
  var candidates=sources.map{$0.appendingPathComponent(entry.path)}
  if entry.path.hasPrefix("Contents/Resources/"),let home=home{candidates.append(home.appendingPathComponent("Library/Application Support/Slice Groups/Models/scnet-v1/"+URL(fileURLWithPath:entry.path).lastPathComponent))}
  for path in candidates where regular(path) {
   guard let bytes=try? Data(contentsOf:path,options:.mappedIfSafe) else{continue}
   if entry.strategy=="reuse" {if bytes.count==entry.size && digest(bytes)==entry.sha256{reused[key]=path;return path}}
   else if entry.strategy=="macho",let prefix=entry.prefix,let end=entry.bodyEnd,let sha=entry.bodySha256,prefix>=0,end>prefix,end<=bytes.count {
    if digest(Data(bytes[prefix..<end]))==sha{reused[key]=path;return path}
   }
  }
  throw UpdateError("A compatible installed dependency is missing: \(URL(fileURLWithPath:entry.path).lastPathComponent). No installed app has been changed. Use the full Slice Groups installer for this update.")
 }
 func check()throws {
  try requireClosed();guard !sources.isEmpty else{throw UpdateError("This is an update-only installer. Install the full Slice Groups package first.")}
  for bundle in manifest.bundles {try checkDestination(destination(bundle.role));for entry in bundle.files {
   switch entry.strategy {case "payload":let bytes=try blob(entry.parts ?? []);guard bytes.count==entry.size,digest(bytes)==entry.sha256 else{throw UpdateError("Payload checksum failed")}
   case "reuse":_ = try reuse(entry)
   case "macho":guard let prefix=entry.prefix,let end=entry.bodyEnd,prefix>=0,end>=prefix,end<=entry.size else{throw UpdateError("Invalid dependency patch")};guard try blob(entry.prefixParts ?? []).count==prefix,try blob(entry.tailParts ?? []).count==entry.size-end else{throw UpdateError("Invalid signature repair data")};_ = try reuse(entry)
   default:throw UpdateError("Unknown update file strategy")}
  }}
  print("Compatible installed dependencies verified for Slice Groups \(manifest.version).",terminator:"\n")
 }
 func assemble(_ bundle:BundleSpec,_ stage:URL)throws {
  try fm.createDirectory(at:stage,withIntermediateDirectories:true)
  for entry in bundle.files {let out=stage.appendingPathComponent(entry.path);try fm.createDirectory(at:out.deletingLastPathComponent(),withIntermediateDirectories:true)
   switch entry.strategy {
   case "payload":try blob(entry.parts ?? []).write(to:out)
   case "reuse":try copyClone(reuse(entry),out)
   case "macho":try copyClone(reuse(entry),out);let handle=try FileHandle(forWritingTo:out);defer{try? handle.close()};try handle.seek(toOffset:0);try handle.write(contentsOf:blob(entry.prefixParts ?? []));try handle.seek(toOffset:UInt64(entry.bodyEnd!));try handle.write(contentsOf:blob(entry.tailParts ?? []));try handle.truncate(atOffset:UInt64(entry.size))
   default:throw UpdateError("Unsupported update strategy")}
   try fm.setAttributes([.posixPermissions:entry.mode & 0o777],ofItemAtPath:out.path)
   guard try fileHash(out)==entry.sha256 else{throw UpdateError("Reconstructed file checksum failed: \(entry.path)")}
  }
  try verify(stage);let info=NSDictionary(contentsOf:stage.appendingPathComponent("Contents/Info.plist"));guard info?["CFBundleShortVersionString"] as? String == manifest.version else{throw UpdateError("Reconstructed bundle version mismatch")}
 }
 func install()throws {
  try check();var staged:[(URL,URL,URL,Bool)]=[];let token=UUID().uuidString
  defer{for (stage,_,_,_) in staged{try? fm.removeItem(at:stage)}}
  for bundle in manifest.bundles {let target=try destination(bundle.role);try fm.createDirectory(at:target.deletingLastPathComponent(),withIntermediateDirectories:true);let stage=target.deletingLastPathComponent().appendingPathComponent(".SliceGroups-update-\(token)"+(bundle.role=="plugin" ? ".vst3":".app"));let backup=target.deletingLastPathComponent().appendingPathComponent(".SliceGroups-previous-\(token)"+(bundle.role=="plugin" ? ".vst3":".app"));staged.append((stage,target,backup,fm.fileExists(atPath:target.path)));try assemble(bundle,stage)}
  try requireClosed();var changed:[(URL,URL,Bool)]=[]
  do {for (stage,target,backup,existed) in staged{if existed{try fm.moveItem(at:target,to:backup)};changed.append((target,backup,existed));try fm.moveItem(at:stage,to:target);try verify(target)
 #if UPDATE_HELPER_TEST
 if changed.count==1 && ProcessInfo.processInfo.environment["SG_UPDATE_TEST_FAIL_COMMIT"]=="1"{throw UpdateError("Injected commit failure")}
 #endif
 }}catch{let original=error;var failures:[String]=[];for (target,backup,existed) in changed.reversed(){do{if fm.fileExists(atPath:target.path){try fm.removeItem(at:target)};if existed{try fm.moveItem(at:backup,to:target)}}catch{failures.append(backup.path)}};if !failures.isEmpty{throw UpdateError("Update failed. Previous copies remain at: "+failures.joined(separator:", "))};throw original}
  print("Installed and verified Slice Groups \(manifest.version): VST3 and standalone. Previous copies retained as hidden rollback bundles.")
 }
}
do {let args=CommandLine.arguments;guard args.count==4,["check","install"].contains(args[1]) else{throw UpdateError("Usage: UpdateHelper check|install payload volume")};let updater=try Updater(URL(fileURLWithPath:args[2]),args[3]);if args[1]=="check"{try updater.check()}else{try updater.install()}}catch{fputs("Slice Groups update: \(error)\n",stderr);exit(1)}
