#pragma once
#include <sqlite3.h>
#include <mutex>
#include <stdexcept>

// One database per local library. SQLite WAL keeps readers independent of writers.
// Bulk metadata snapshots are immutable; sample recall and saves are point queries.
class LocalDatabase {
 struct Connection {
  sqlite3* db=nullptr;
  Connection(juce::File file,bool write=true){int flags=(write?SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE:SQLITE_OPEN_READONLY)|SQLITE_OPEN_FULLMUTEX;if(sqlite3_open_v2(file.getFullPathName().toRawUTF8(),&db,flags,nullptr)!=SQLITE_OK){auto message=juce::String(db?sqlite3_errmsg(db):"Unable to open database");if(db)sqlite3_close(db);db=nullptr;throw std::runtime_error(message.toStdString());}sqlite3_busy_timeout(db,1000);}
  ~Connection(){if(db)sqlite3_close(db);}Connection(const Connection&)=delete;
  void exec(const char* sql){char* error=nullptr;if(sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK){std::string message=error?error:sqlite3_errmsg(db);sqlite3_free(error);throw std::runtime_error(message);}}
 };
 struct Statement {
  sqlite3_stmt* statement=nullptr;
  Statement(sqlite3* db,const char* sql){if(sqlite3_prepare_v2(db,sql,-1,&statement,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
  ~Statement(){sqlite3_finalize(statement);}void bind(int n,const juce::String& value){if(sqlite3_bind_text(statement,n,value.toRawUTF8(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error("SQLite text bind failed");}
  void integer(int n,sqlite3_int64 v){sqlite3_bind_int64(statement,n,v);}bool row(){int code=sqlite3_step(statement);if(code==SQLITE_ROW)return true;if(code==SQLITE_DONE)return false;throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(statement)));}
  void run(){row();sqlite3_reset(statement);sqlite3_clear_bindings(statement);}juce::String text(int n){auto value=sqlite3_column_text(statement,n);return value?juce::String::fromUTF8((const char*)value):juce::String();}sqlite3_int64 number(int n){return sqlite3_column_int64(statement,n);}
 };
 juce::File folder,file;std::unique_ptr<Connection> anchor;std::mutex writeMutex,cacheMutex,pollMutex;std::map<juce::String,uint64_t> revisions;std::set<juce::String> pendingNotifications;double polled=0;
 struct Cached{uint64_t revision=0;juce::var value;};std::map<juce::String,Cached> snapshots;
 std::map<juce::String,std::shared_ptr<std::mutex>> namespaceLocks;
 static juce::var empty(){return juce::var(new juce::DynamicObject());}
 static void schema(Connection& c){c.exec("CREATE TABLE IF NOT EXISTS metadata(kind TEXT NOT NULL, identity TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(kind,identity)) WITHOUT ROWID; CREATE TABLE IF NOT EXISTS loops(identity TEXT PRIMARY KEY, xml TEXT NOT NULL, musical_key INTEGER NOT NULL DEFAULT -1) WITHOUT ROWID; CREATE TABLE IF NOT EXISTS revisions(kind TEXT PRIMARY KEY, revision INTEGER NOT NULL) WITHOUT ROWID; CREATE TABLE IF NOT EXISTS catalog_roots(root TEXT PRIMARY KEY,generation TEXT NOT NULL) WITHOUT ROWID; CREATE TABLE IF NOT EXISTS catalog(root TEXT NOT NULL,generation TEXT NOT NULL,path TEXT NOT NULL,parent TEXT NOT NULL,name TEXT NOT NULL,is_folder INTEGER NOT NULL,PRIMARY KEY(root,generation,path)) WITHOUT ROWID; CREATE INDEX IF NOT EXISTS catalog_parent ON catalog(root,generation,parent,name COLLATE NOCASE); CREATE INDEX IF NOT EXISTS catalog_name ON catalog(root,generation,name COLLATE NOCASE); PRAGMA user_version=2;");}
 static void bump(Connection& c,juce::String kind){Statement s(c.db,"INSERT INTO revisions(kind,revision) VALUES(?,1) ON CONFLICT(kind) DO UPDATE SET revision=revision+1");s.bind(1,kind);s.run();}
 void invalidate(juce::String kind){std::lock_guard<std::mutex> guard(cacheMutex);snapshots.erase(kind);polled=0;}
 uint64_t revision(juce::String kind){std::lock_guard<std::mutex> guard(cacheMutex);return revisions[kind];}
 static juce::var decodeValue(juce::String text){auto parsed=juce::JSON::parse("["+text+"]");if(auto* values=parsed.getArray())if(values->size()==1)return (*values)[0];throw std::runtime_error("Invalid database metadata value");}
 static void putMetadata(Connection& c,juce::String kind,juce::String key,const juce::var& value){Statement s(c.db,"INSERT INTO metadata(kind,identity,value) VALUES(?,?,?) ON CONFLICT(kind,identity) DO UPDATE SET value=excluded.value");s.bind(1,kind);s.bind(2,key);s.bind(3,juce::JSON::toString(value,true));s.run();}
 static void putLoop(Connection& c,juce::String identity,juce::String xml){auto config=juce::parseXML(xml);if(!config)throw std::runtime_error("Invalid saved sample XML: "+identity.toStdString());Statement s(c.db,"INSERT INTO loops(identity,xml,musical_key) VALUES(?,?,?) ON CONFLICT(identity) DO UPDATE SET xml=excluded.xml,musical_key=excluded.musical_key");s.bind(1,identity);s.bind(2,xml);s.integer(3,config->getIntAttribute("loopKey",-1));s.run();}
 struct CatalogEntry{juce::String path,parent,name;bool folder=false;};
 static std::vector<CatalogEntry> catalogEntries(const juce::Array<juce::File>& files){std::vector<CatalogEntry> entries;entries.reserve(files.size());for(auto file:files)entries.push_back({file.getFullPathName(),file.getParentDirectory().getFullPathName(),file.getFileName(),file.isDirectory()});return entries;}
 static void insertCatalog(Connection& c,juce::String root,juce::String generation,const std::vector<CatalogEntry>& entries,size_t begin,size_t end){Statement entry(c.db,"INSERT OR IGNORE INTO catalog(root,generation,path,parent,name,is_folder) VALUES(?,?,?,?,?,?)");for(size_t n=begin;n<end;++n){auto& f=entries[n];entry.bind(1,root);entry.bind(2,generation);entry.bind(3,f.path);entry.bind(4,f.parent);entry.bind(5,f.name);entry.integer(6,f.folder);entry.run();}}
 static juce::String publishCatalog(Connection& c,juce::String root,juce::String generation){juce::String old;{Statement current(c.db,"SELECT generation FROM catalog_roots WHERE root=?");current.bind(1,root);if(current.row())old=current.text(0);}Statement mark(c.db,"INSERT INTO catalog_roots(root,generation) VALUES(?,?) ON CONFLICT(root) DO UPDATE SET generation=excluded.generation");mark.bind(1,root);mark.bind(2,generation);mark.run();return old;}
 static void putCatalog(Connection& c,juce::File root,const juce::Array<juce::File>& files){auto entries=catalogEntries(files);auto generation=juce::Uuid().toString();insertCatalog(c,root.getFullPathName(),generation,entries,0,entries.size());publishCatalog(c,root.getFullPathName(),generation);}
 void retireCatalog(juce::String root,juce::String generation){if(generation.isEmpty())return;for(;;){int removed=0;{std::lock_guard<std::mutex> guard(writeMutex);Connection c(file);Statement s(c.db,"DELETE FROM catalog WHERE root=? AND generation=? AND path IN (SELECT path FROM catalog WHERE root=? AND generation=? LIMIT 256)");s.bind(1,root);s.bind(2,generation);s.bind(3,root);s.bind(4,generation);s.run();removed=sqlite3_changes(c.db);}if(!removed)break;juce::Thread::yield();}}
 void migrate(){
  auto staged=folder.getChildFile(".library-migration-"+juce::Uuid().toString()+".db");auto backup=folder.getChildFile("storage-before-sqlite-"+juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+"-"+juce::Uuid().toString().substring(0,8));
  if(!backup.createDirectory())throw std::runtime_error("Could not create metadata backup");
  std::map<juce::String,juce::var> values;
  try{
   for(auto entry:juce::RangedDirectoryIterator(folder,false,"*.json",juce::File::findFiles)){
    auto source=entry.getFile();if(!source.copyFileTo(backup.getChildFile(source.getFileName())))throw std::runtime_error("Could not back up "+source.getFileName().toStdString());auto content=source.loadFileAsString();auto value=juce::JSON::parse(content);if(!value.isObject())throw std::runtime_error("Invalid metadata JSON: "+source.getFileName().toStdString());values[source.getFileName()]=value;
   }
   auto configs=folder.getChildFile("loop-configs-v2");if(configs.isDirectory()&&!configs.copyDirectoryTo(backup.getChildFile("loop-configs-v2")))throw std::runtime_error("Could not back up v2 settings");
   auto search=folder.getChildFile("search-index");if(search.isDirectory()&&!search.copyDirectoryTo(backup.getChildFile("search-index")))throw std::runtime_error("Could not back up browser index");
   auto& loops=values["loops.json"];if(!loops.isObject())loops=empty();
   // JSON is authoritative after the user's JSON trial. V2 only fills missing records.
   if(auto* index=values["loop-index-v2.json"].getDynamicObject())for(auto& p:index->getProperties()){
    auto path=p.name.toString();auto f=configs.getChildFile(juce::SHA256(("path:"+path).toRawUTF8(),("path:"+path).getNumBytesAsUTF8()).toHexString()+".xml");
    if(!loops.getDynamicObject()->hasProperty(path)){auto xml=f.loadFileAsString();auto config=juce::parseXML(xml);if(!config)throw std::runtime_error("Missing v2 saved settings for "+path.toStdString());loops.getDynamicObject()->setProperty(path,xml);auto hash=config->getStringAttribute("contentHash");if(hash.isNotEmpty()&&!loops.getDynamicObject()->hasProperty("sha256:"+hash))loops.getDynamicObject()->setProperty("sha256:"+hash,xml);}
   }
   {Connection c(staged);schema(c);c.exec("BEGIN IMMEDIATE");try{
     for(auto& pair:values){if(pair.first=="loop-index-v2.json")continue;auto* o=pair.second.getDynamicObject();if(!o)continue;for(auto& p:o->getProperties())if(pair.first=="loops.json")putLoop(c,p.name.toString(),p.value.toString());else putMetadata(c,pair.first,p.name.toString(),p.value);bump(c,pair.first);}
     if(auto* roots=values["library-roots.json"].getDynamicObject())for(auto& p:roots->getProperties())if((bool)p.value){juce::File root(p.name.toString());auto old=search.getChildFile(juce::String::toHexString(root.getFullPathName().hashCode64())+".json");if(old.existsAsFile()){auto array=juce::JSON::parse(old);if(auto* items=array.getArray()){juce::Array<juce::File> files;for(auto value:*items)files.add(juce::File(value.toString()));putCatalog(c,root,files);}}}
     putMetadata(c,"database-info","migration-backup",backup.getFullPathName());c.exec("COMMIT");
    }catch(...){c.exec("ROLLBACK");throw;}
    Statement verify(c.db,"PRAGMA integrity_check");if(!verify.row()||verify.text(0)!="ok")throw std::runtime_error("Database integrity check failed");
   }
   if(!staged.moveFileTo(file))throw std::runtime_error("Could not install migrated database");
  }catch(...){staged.deleteFile();throw;}
 }
public:
 explicit LocalDatabase(juce::File root):folder(root),file(root.getChildFile("library.db")){
  if(!folder.createDirectory())throw std::runtime_error("Could not create local library folder");juce::InterProcessLock lock("SliceGroupsDatabaseMigration-"+juce::String(folder.getFullPathName().hashCode64()));if(!lock.enter(30000))throw std::runtime_error("Database migration is busy");
  try{if(!file.existsAsFile())migrate();Connection c(file);{Statement version(c.db,"PRAGMA user_version");if(!version.row()||version.number(0)!=2)throw std::runtime_error("Unsupported library database version");}c.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");anchor=std::make_unique<Connection>(file);anchor->exec("PRAGMA synchronous=FULL;");lock.exit();}catch(...){lock.exit();throw;}
 }
 static std::shared_ptr<LocalDatabase> open(juce::File folder){static std::mutex mutex;static std::map<juce::String,std::shared_ptr<LocalDatabase>> instances;std::lock_guard<std::mutex> guard(mutex);auto& instance=instances[folder.getFullPathName()];if(!instance)instance=std::make_shared<LocalDatabase>(folder);return instance;}
 juce::String integrity(){Connection c(file,false);Statement s(c.db,"PRAGMA integrity_check");return s.row()?s.text(0):"failed";}
 std::vector<juce::String> poll(bool force=false){std::unique_lock<std::mutex> polling(pollMutex,std::defer_lock);if(force)polling.lock();else if(!polling.try_lock())return {};auto now=juce::Time::getMillisecondCounterHiRes();{std::lock_guard<std::mutex> guard(cacheMutex);if(!force&&now-polled<250)return {};polled=now;}
  Connection c(file,false);Statement s(c.db,"SELECT kind,revision FROM revisions");std::vector<std::pair<juce::String,uint64_t>> found;while(s.row())found.push_back({s.text(0),(uint64_t)s.number(1)});std::vector<juce::String> changed;
  {std::lock_guard<std::mutex> guard(cacheMutex);for(auto& pair:found)if(revisions[pair.first]!=pair.second){revisions[pair.first]=pair.second;snapshots.erase(pair.first);changed.push_back(pair.first);pendingNotifications.insert(pair.first);}}return changed;
 }
 std::vector<juce::String> takeChanges(){poll();std::lock_guard<std::mutex> guard(cacheMutex);std::vector<juce::String> changes(pendingNotifications.begin(),pendingNotifications.end());pendingNotifications.clear();return changes;}
 juce::var snapshot(juce::String kind){poll();auto serial=revision(kind);{std::lock_guard<std::mutex> guard(cacheMutex);auto it=snapshots.find(kind);if(it!=snapshots.end()&&it->second.revision==serial)return it->second.value;}
  auto value=empty();{Connection c(file,false);Statement s(c.db,kind=="loops.json"?"SELECT identity,xml FROM loops":"SELECT identity,value FROM metadata WHERE kind=?");if(kind!="loops.json")s.bind(1,kind);while(s.row())value.getDynamicObject()->setProperty(s.text(0),kind=="loops.json"?juce::var(s.text(1)):decodeValue(s.text(1)));}
  {std::lock_guard<std::mutex> guard(cacheMutex);if(revisions[kind]==serial)snapshots[kind]={serial,value};}return value;
 }
 bool mutate(juce::String kind,std::function<void(juce::DynamicObject&)> change){
  std::shared_ptr<std::mutex> ns;{std::lock_guard<std::mutex> guard(cacheMutex);auto& ptr=namespaceLocks[kind];if(!ptr)ptr=std::make_shared<std::mutex>();ns=ptr;}std::lock_guard<std::mutex> namespaceGuard(*ns);
  juce::InterProcessLock exclusive("SliceGroupsDatabaseMetadata-"+juce::String((folder.getFullPathName()+kind).hashCode64()));if(!exclusive.enter(1000))return false;
  try{poll(true);auto before=snapshot(kind),after=before.clone();change(*after.getDynamicObject());std::vector<std::pair<juce::String,juce::var>> changed;std::vector<juce::String> removed;
   for(auto& p:after.getDynamicObject()->getProperties())if(!before.getDynamicObject()->hasProperty(p.name)||before.getDynamicObject()->getProperty(p.name)!=p.value)changed.push_back({p.name.toString(),p.value});for(auto& p:before.getDynamicObject()->getProperties())if(!after.getDynamicObject()->hasProperty(p.name))removed.push_back(p.name.toString());
   if(changed.empty()&&removed.empty()){exclusive.exit();return true;}
   {std::lock_guard<std::mutex> guard(writeMutex);Connection c(file);c.exec("BEGIN IMMEDIATE");try{for(auto& p:changed)if(kind=="loops.json")putLoop(c,p.first,p.second.toString());else putMetadata(c,kind,p.first,p.second);for(auto key:removed){Statement s(c.db,kind=="loops.json"?"DELETE FROM loops WHERE identity=?":"DELETE FROM metadata WHERE identity=? AND kind=?");s.bind(1,key);if(kind!="loops.json")s.bind(2,kind);s.run();}bump(c,kind);c.exec("COMMIT");}catch(...){c.exec("ROLLBACK");throw;}}
   invalidate(kind);poll(true);
   // Publish the value already constructed by the writer, so UI readers never
   // have to rebuild a complete metadata namespace immediately after an edit.
   auto serial=revision(kind);{std::lock_guard<std::mutex> guard(cacheMutex);snapshots[kind]={serial,after};}
   exclusive.exit();return true;
  }catch(...){exclusive.exit();throw;}
 }
 bool saveLoop(juce::String path,juce::String hash,juce::String xml){std::lock_guard<std::mutex> guard(writeMutex);Connection c(file);c.exec("BEGIN IMMEDIATE");try{putLoop(c,path,xml);if(hash.isNotEmpty())putLoop(c,"sha256:"+hash,xml);bump(c,"loops.json");c.exec("COMMIT");}catch(...){c.exec("ROLLBACK");throw;}invalidate("loops.json");return true;}
 juce::String loop(juce::String identity){Connection c(file,false);Statement s(c.db,"SELECT xml FROM loops WHERE identity=?");s.bind(1,identity);return s.row()?s.text(0):juce::String();}
 juce::var savedKeys(){auto value=empty();Connection c(file,false);Statement s(c.db,"SELECT identity,musical_key FROM loops WHERE identity LIKE '/%'");while(s.row())value.getDynamicObject()->setProperty(s.text(0),(int)s.number(1));return value;}
 bool hasCatalog(juce::File root){Connection c(file,false);Statement s(c.db,"SELECT 1 FROM catalog_roots WHERE root=?");s.bind(1,root.getFullPathName());return s.row();}
 juce::Array<juce::File> catalog(juce::File root){juce::Array<juce::File> result;Connection c(file,false);Statement s(c.db,"SELECT c.path FROM catalog c JOIN catalog_roots r ON c.root=r.root AND c.generation=r.generation WHERE c.root=? ORDER BY c.name COLLATE NOCASE,c.path COLLATE NOCASE");s.bind(1,root.getFullPathName());while(s.row())result.add(juce::File(s.text(0)));return result;}
 void setCatalog(juce::File root,const juce::Array<juce::File>& files){
  // Filesystem work happens before taking the writer lock. Publish a complete
  // generation atomically, but insert it in short batches so UI writes can run.
  auto entries=catalogEntries(files);auto path=root.getFullPathName(),generation=juce::Uuid().toString();bool published=false;
  try{for(size_t begin=0;begin<entries.size();begin+=256){{std::lock_guard<std::mutex> guard(writeMutex);Connection c(file);c.exec("BEGIN IMMEDIATE");try{insertCatalog(c,path,generation,entries,begin,std::min(entries.size(),begin+256));c.exec("COMMIT");}catch(...){c.exec("ROLLBACK");throw;}}juce::Thread::yield();}
   juce::String old;{std::lock_guard<std::mutex> guard(writeMutex);Connection c(file);c.exec("BEGIN IMMEDIATE");try{old=publishCatalog(c,path,generation);c.exec("COMMIT");}catch(...){c.exec("ROLLBACK");throw;}}published=true;try{retireCatalog(path,old);}catch(const std::exception& e){std::cerr<<"Retired catalogue cleanup: "<<e.what()<<"\n";}
  }catch(...){if(!published)try{retireCatalog(path,generation);}catch(...){}throw;}
 }
 void invalidateCatalog(juce::File root){juce::String generation;{std::lock_guard<std::mutex> guard(writeMutex);Connection c(file);c.exec("BEGIN IMMEDIATE");try{{Statement read(c.db,"SELECT generation FROM catalog_roots WHERE root=?");read.bind(1,root.getFullPathName());if(read.row())generation=read.text(0);}Statement s(c.db,"DELETE FROM catalog_roots WHERE root=?");s.bind(1,root.getFullPathName());s.run();c.exec("COMMIT");}catch(...){c.exec("ROLLBACK");throw;}}retireCatalog(root.getFullPathName(),generation);}
};
