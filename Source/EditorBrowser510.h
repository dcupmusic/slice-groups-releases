// Two lightweight view snapshots per editor/group; shared index services remain alive.
struct BrowserView510 {bool historyCached525=true;bool valid=false,pending=false,folderPending517=false;uint64_t diskRevision=0,metadataRevision=0;juce::String query,scopeQuery,pathText,folderText;bool favourites=false;int countedRows=-1;juce::String resultsText;juce::SparseSet<int> selectedRows;juce::Point<int> scroll,folderScroll;juce::Array<juce::File> selection;
decltype(BrowserState::directory) directory;
decltype(BrowserState::searchBranch) searchBranch;
decltype(BrowserState::openFolders) openFolders;
decltype(BrowserState::activeTags) activeTags;
decltype(BrowserState::excludedTags) excludedTags;
decltype(BrowserState::selectedFolders) selectedFolders;
decltype(BrowserState::folderEntries) folderEntries;
decltype(BrowserState::folderDepth) folderDepth;
decltype(BrowserState::folderChildren) folderChildren;
decltype(BrowserState::folderTreeSignature) folderTreeSignature;
decltype(BrowserState::matchedFolderEntries) matchedFolderEntries;
decltype(BrowserState::matchingSearchFolders) matchingSearchFolders;
decltype(BrowserState::matchingFoldersRevision) matchingFoldersRevision;
decltype(BrowserState::pruneSearchFolders) pruneSearchFolders;
decltype(BrowserState::recursiveLibrary) recursiveLibrary;
decltype(BrowserState::recursiveFolders) recursiveFolders;
decltype(BrowserState::recursiveRoot) recursiveRoot;
decltype(BrowserState::recursiveReady) recursiveReady;
decltype(BrowserState::recursiveNameCounts) recursiveNameCounts;
decltype(BrowserState::indexedFolderPaths) indexedFolderPaths;
decltype(BrowserState::library) library;
decltype(BrowserState::visible) visible;
decltype(BrowserState::searchCatalog) searchCatalog;
decltype(BrowserState::searchCatalogSignature) searchCatalogSignature;
decltype(BrowserState::searchCatalogTypeRevision) searchCatalogTypeRevision;
decltype(BrowserState::presetsView) presetsView;
decltype(BrowserState::presetNames) presetNames;
decltype(BrowserState::sampleTypeFilter) sampleTypeFilter;
decltype(BrowserState::sampleTypeMask) sampleTypeMask;
decltype(BrowserState::colourFilterMask) colourFilterMask;
decltype(BrowserState::browserSortColumn) browserSortColumn;
decltype(BrowserState::browserSortDescending) browserSortDescending;
decltype(BrowserState::tagSearchOr) tagSearchOr;
decltype(BrowserState::folderSearchMode) folderSearchMode;
};
struct History525 {std::vector<std::shared_ptr<BrowserView510>> back;std::shared_ptr<BrowserView510> current;bool typing=false;double lastType=0;};
std::array<std::shared_ptr<History525>,3> histories525;bool historyReady525=false,historyRestoring525=false;
History525& history525(){auto& ptr=histories525[presetsView?2:(viewContextMidi510?1:0)];if(!ptr)ptr=std::make_shared<History525>();return *ptr;}
bool sameHistory525(const BrowserView510& v,bool ignoreText=false)const{return v.directory==directory&&(v.searchBranch==searchBranch||(ignoreText&&v.scopeQuery!=folderSearch.getText()))&&v.selectedFolders==selectedFolders&&(ignoreText||(v.query==search.getText()&&v.scopeQuery==folderSearch.getText()))&&v.activeTags==activeTags&&v.excludedTags==excludedTags&&v.favourites==favouriteFilter.getToggleState()&&v.sampleTypeMask==sampleTypeMask&&v.colourFilterMask==colourFilterMask&&v.tagSearchOr==tagSearchOr&&v.folderSearchMode==folderSearchMode&&v.presetsView==presetsView&&v.browserSortColumn==browserSortColumn&&v.browserSortDescending==browserSortDescending;}
void historyCache525(BrowserView510& v,bool restore){
 if(restore){v.folderEntries=folderEntries;v.folderDepth=folderDepth;v.folderChildren=folderChildren;v.matchedFolderEntries=matchedFolderEntries;v.matchingSearchFolders=matchingSearchFolders;v.recursiveLibrary=recursiveLibrary;v.recursiveFolders=recursiveFolders;v.recursiveNameCounts=recursiveNameCounts;v.indexedFolderPaths=indexedFolderPaths;v.library=library;v.visible=visible;v.searchCatalog=searchCatalog;v.searchCatalogSignature=searchCatalogSignature;v.searchCatalogTypeRevision=searchCatalogTypeRevision;v.recursiveRoot=juce::File();v.recursiveReady=false;v.pending=true;v.diskRevision=~uint64_t{};v.historyCached525=true;}
 else{v.folderEntries.clear();v.folderDepth.clear();v.folderChildren.clear();v.matchedFolderEntries.clear();v.matchingSearchFolders.clear();v.recursiveLibrary.clear();v.recursiveFolders.clear();v.recursiveNameCounts.clear();v.indexedFolderPaths.clear();v.library.clear();v.visible.clear();v.searchCatalog.reset();v.historyCached525=false;}
}
void trimHistory525(History525& h){while(h.back.size()>50)h.back.erase(h.back.begin());int cached=0;size_t paths=0;for(auto i=h.back.rbegin();i!=h.back.rend();++i)if((*i)->historyCached525){paths+=(*i)->visible.size();if(++cached>8||paths>200000)historyCache525(**i,false);}}
void updateBack525(){if(!historyReady525)return;auto& h=history525();searchBack525.setEnabled(!h.back.empty()||(h.current&&!sameHistory525(*h.current)));}
void alignHistory525(){if(!historyReady525||historyRestoring525)return;auto& h=history525();h.typing=false;if(!h.current)h.current=std::make_shared<BrowserView510>();fillBrowserView525(*h.current,true);updateBack525();}
void observeHistory525(bool text=false,bool tick=false,bool force=false){if(!historyReady525||historyRestoring525||browserState->target.getComponent()!=this)return;auto& h=history525();if(!h.current){alignHistory525();return;}auto now=juce::Time::getMillisecondCounterHiRes();if(sameHistory525(*h.current)){h.typing=false;if(tick)fillBrowserView525(*h.current,true);updateBack525();return;}if(text){h.typing=true;h.lastType=now;updateBack525();return;}if(h.typing&&sameHistory525(*h.current,true)&&!force&&(!tick||now-h.lastType<500)){updateBack525();return;}h.back.push_back(std::move(h.current));h.current=std::make_shared<BrowserView510>();fillBrowserView525(*h.current,true);h.current->pending=true;h.typing=false;trimHistory525(h);updateBack525();}
void searchBackAction525(){if(!historyReady525)return;observeHistory525(false,false,true);auto& h=history525();if(h.back.empty())return;auto target=h.back.back();h.back.pop_back();{juce::ScopedValueSetter<bool> guard(historyRestoring525,true);if(!target->historyCached525)historyCache525(*target,true);restoreBrowser510(target.get(),true);}h.current=target;h.typing=false;updateBack525();}
struct HistorySwitch525 {Editor& e;bool previous;explicit HistorySwitch525(Editor& editor):e(editor),previous(editor.historyRestoring525){if(!previous)e.observeHistory525(false,true,true);e.historyRestoring525=true;}~HistorySwitch525(){e.historyRestoring525=previous;if(!previous)e.alignHistory525();}};
std::array<BrowserView510,2> browserViews510;bool viewContextMidi510=false,separateMidiBrowser511=false;
juce::File contextFile510()const{if(p.sequenceView)return p.sequence.path.isEmpty()?juce::File():LibraryStore::resolveFile(juce::File(p.sequence.path));auto a=p.getAudio();return a?LibraryStore::resolveFile(juce::File(a->path)):juce::File();}
bool sameBrowserRows510(const BrowserView510& v)const{return v.valid&&v.directory==directory&&v.searchCatalog==searchCatalog&&v.visible.size()==visible.size()&&v.query==search.getText()&&v.scopeQuery==folderSearch.getText()&&v.searchBranch==searchBranch&&v.activeTags==activeTags&&v.excludedTags==excludedTags&&v.selectedFolders==selectedFolders&&v.sampleTypeMask==sampleTypeMask&&v.colourFilterMask==colourFilterMask&&v.browserSortColumn==browserSortColumn&&v.browserSortDescending==browserSortDescending&&v.favourites==favouriteFilter.getToggleState()&&v.presetsView==presetsView&&v.tagSearchOr==tagSearchOr;}
void captureBrowser510(){if(presetsView&&!browserState->presetView515)browserState->presetView515=std::make_shared<BrowserView510>();auto& v=presetsView?*std::static_pointer_cast<BrowserView510>(browserState->presetView515):browserViews510[viewContextMidi510&&separateMidiBrowser511?1:0];fillBrowserView525(v);}
void fillBrowserView525(BrowserView510& v,bool lightweight=false){bool sameRows=sameBrowserRows510(v)&&(!lightweight||!v.pending),sameCatalog=v.valid&&v.directory==directory&&v.searchCatalog==searchCatalog&&v.recursiveRoot==recursiveRoot;if(lightweight)sameCatalog=v.valid&&v.directory==directory&&v.recursiveRoot==recursiveRoot&&v.diskRevision==browserState->diskRevision510&&v.recursiveReady==recursiveReady&&v.recursiveLibrary.size()==recursiveLibrary.size()&&v.recursiveFolders.size()==recursiveFolders.size()&&v.library.size()==library.size();bool sameTree=lightweight&&v.valid&&v.directory==directory&&v.diskRevision==browserState->diskRevision510&&v.folderTreeSignature==folderTreeSignature&&v.matchingFoldersRevision==matchingFoldersRevision&&v.folderEntries.size()==folderEntries.size();bool sameSelection=lightweight&&sameRows&&v.selectedRows==files.getSelectedRows();v.valid=true;v.folderPending517=browserState->folderRefreshPending517;v.diskRevision=browserState->diskRevision510;v.metadataRevision=browserDataRevision();v.pending=browserState->folderRefreshPending517||browserState->searchFilterPool.getNumJobs()>0||pendingCatalogSignature.isNotEmpty()||!recursiveReady;v.countedRows=countedRows;v.resultsText=resultsText;v.selectedRows=files.getSelectedRows();v.query=search.getText();v.scopeQuery=folderSearch.getText();v.pathText=path.getText();v.folderText=folderLabel.getText();v.favourites=favouriteFilter.getToggleState();v.scroll=files.getViewport()->getViewPosition();v.folderScroll=folderList.getViewport()->getViewPosition();if(!sameSelection){v.selection.clear();auto rows=files.getSelectedRows();for(int i=0;i<rows.size();++i)if(juce::isPositiveAndBelow(rows[i],visible.size()))v.selection.add(visible[rows[i]]);}
v.directory=browserState->directory;
v.searchBranch=browserState->searchBranch;
v.openFolders=browserState->openFolders;
v.activeTags=browserState->activeTags;
v.excludedTags=browserState->excludedTags;
v.selectedFolders=browserState->selectedFolders;
if(!sameTree)v.folderEntries=browserState->folderEntries;
if(!sameTree)v.folderDepth=browserState->folderDepth;
if(!sameTree)v.folderChildren=browserState->folderChildren;
v.folderTreeSignature=browserState->folderTreeSignature;
if(!sameTree)v.matchedFolderEntries=browserState->matchedFolderEntries;
if(!sameTree)v.matchingSearchFolders=browserState->matchingSearchFolders;
v.matchingFoldersRevision=browserState->matchingFoldersRevision;
v.pruneSearchFolders=browserState->pruneSearchFolders;
if(!sameCatalog)v.recursiveLibrary=browserState->recursiveLibrary;
if(!sameCatalog)v.recursiveFolders=browserState->recursiveFolders;
if(!sameCatalog)v.recursiveRoot=browserState->recursiveRoot;
if(!sameCatalog)v.recursiveReady=browserState->recursiveReady;
if(!sameCatalog)v.recursiveNameCounts=browserState->recursiveNameCounts;
if(!sameCatalog)v.indexedFolderPaths=browserState->indexedFolderPaths;
if(!sameCatalog)v.library=browserState->library;
if(!sameRows)v.visible=browserState->visible;
if(!sameCatalog||lightweight)v.searchCatalog=browserState->searchCatalog;
if(!sameCatalog||lightweight)v.searchCatalogSignature=browserState->searchCatalogSignature;
if(!sameCatalog||lightweight)v.searchCatalogTypeRevision=browserState->searchCatalogTypeRevision;
v.presetsView=browserState->presetsView;
v.presetNames=browserState->presetNames;
v.sampleTypeFilter=browserState->sampleTypeFilter;
v.sampleTypeMask=browserState->sampleTypeMask;
v.colourFilterMask=browserState->colourFilterMask;
v.browserSortColumn=browserState->browserSortColumn;
v.browserSortDescending=browserState->browserSortDescending;
v.tagSearchOr=browserState->tagSearchOr;
v.folderSearchMode=browserState->folderSearchMode;
}
void restoreBrowser510(BrowserView510* overrideView=nullptr,bool exactSelection525=false){auto& v=overrideView?*overrideView:browserViews510[viewContextMidi510&&separateMidiBrowser511?1:0];if(!v.valid){captureBrowser510();return;}v.directory=LibraryStore::resolveFile(v.directory);if(v.searchBranch!=juce::File())v.searchBranch=LibraryStore::resolveFile(v.searchBranch);for(auto& f:v.selectedFolders)f=LibraryStore::resolveFile(f);{std::map<juce::String,juce::File> expanded;for(auto& pair:v.openFolders)expanded[LibraryStore::resolveFile(juce::File(pair.first)).getFullPathName()]=LibraryStore::resolveFile(pair.second);v.openFolders=std::move(expanded);}juce::ScopedValueSetter<bool> guard(suppressBrowserLoad,true);bool sameRows=sameBrowserRows510(v),sameCatalog=v.directory==directory&&v.searchCatalog==searchCatalog&&v.recursiveRoot==recursiveRoot;bool diskChanged=v.diskRevision!=browserState->diskRevision510,metadataChanged=v.metadataRevision!=browserDataRevision();if(!v.presetsView&&(diskChanged||v.folderPending517))browserState->folderRefreshPending517=true;auto priorRoot=directory;++*searchFilterGeneration;++catalogBuildToken;++sortToken;pendingCatalogSignature.clear();metadataScope.clear();metadataCount=-1;
browserState->directory=v.directory;
browserState->searchBranch=v.searchBranch;
browserState->openFolders=v.openFolders;
browserState->activeTags=v.activeTags;
browserState->excludedTags=v.excludedTags;
browserState->selectedFolders=v.selectedFolders;
browserState->folderEntries=v.folderEntries;
browserState->folderDepth=v.folderDepth;
browserState->folderChildren=v.folderChildren;
browserState->folderTreeSignature=v.folderTreeSignature;
browserState->matchedFolderEntries=v.matchedFolderEntries;
browserState->matchingSearchFolders=v.matchingSearchFolders;
browserState->matchingFoldersRevision=v.matchingFoldersRevision;
browserState->pruneSearchFolders=v.pruneSearchFolders;
if(!sameCatalog)browserState->recursiveLibrary=v.recursiveLibrary;
if(!sameCatalog)browserState->recursiveFolders=v.recursiveFolders;
if(!sameCatalog)browserState->recursiveRoot=v.recursiveRoot;
if(!sameCatalog)browserState->recursiveReady=v.recursiveReady;
if(!sameCatalog)browserState->recursiveNameCounts=v.recursiveNameCounts;
if(!sameCatalog)browserState->indexedFolderPaths=v.indexedFolderPaths;
if(!sameCatalog)browserState->library=v.library;
if(!sameRows)browserState->visible=v.visible;
if(!sameCatalog)browserState->searchCatalog=v.searchCatalog;
if(!sameCatalog)browserState->searchCatalogSignature=v.searchCatalogSignature;
if(!sameCatalog)browserState->searchCatalogTypeRevision=v.searchCatalogTypeRevision;
browserState->presetsView=v.presetsView;
browserState->presetNames=v.presetNames;
browserState->sampleTypeFilter=v.sampleTypeFilter;
browserState->sampleTypeMask=v.sampleTypeMask;
browserState->colourFilterMask=v.colourFilterMask;
browserState->browserSortColumn=v.browserSortColumn;
browserState->browserSortDescending=v.browserSortDescending;
browserState->tagSearchOr=v.tagSearchOr;
browserState->folderSearchMode=v.folderSearchMode;

revealingFile=juce::File();search.setText(v.query,false);folderSearch.setText(v.scopeQuery,false);path.setText(v.pathText,false);folderLabel.setText(v.folderText,juce::dontSendNotification);favouriteFilter.setToggleState(v.favourites,juce::dontSendNotification);tagLogic.setButtonText(tagSearchOr?"OR":"AND");updateTypeFilterLabel();updateSearchTag();files.updateContent();folderList.updateContent();auto selection=v.selectedRows;auto current=contextFile510();int currentRow=visible.indexOf(current);if(!exactSelection525&&currentRow>=0){selection.clear();selection.addRange({currentRow,currentRow+1});}files.setSelectedRows(selection,juce::dontSendNotification);juce::SparseSet<int> folders;for(auto f:selectedFolders){int row=folderEntries.indexOf(f);if(row>=0)folders.addRange({row,row+1});}folderList.setSelectedRows(folders,juce::dontSendNotification);updateContentWidth();resized();files.getViewport()->setViewPosition(v.scroll);folderList.getViewport()->setViewPosition(v.folderScroll);requestedFile=current;p.browserPath=directory.getFullPathName();countedRows=v.countedRows;resultsText=v.resultsText;statusUntil=0;status.setText(resultsText,juce::dontSendNotification);if(!presetsView&&(!recursiveReady||diskChanged||browserState->folderRefreshPending517))searchIndexToken=browserIndex.request(directory);if(!presetsView&&metadataChanged)requestMetadataRefresh();if(!presetsView&&v.pending&&recursiveReady){sortBrowserRows();filterFiles(true,false);}files.repaint();folderList.repaint();}
void switchBrowserView510(bool midi){HistorySwitch525 transition(*this);if(presetsView){viewContextMidi510=midi;p.sequenceView=midi;resized();return;}if(viewContextMidi510==midi){p.sequenceView=midi;resized();return;}captureBrowser510();viewContextMidi510=midi;p.sequenceView=midi;restoreBrowser510();refresh();resized();}
void syncBrowserView510(){if(viewContextMidi510!=p.sequenceView)switchBrowserView510(p.sequenceView);}
void noteBrowserSelection511(juce::File file){HistorySwitch525 transition(*this);if(presetsView||!p.sequenceView)return;separateMidiBrowser511=SequenceClip::midi(file);captureBrowser510();}
void loadedMidiBrowser510(){HistorySwitch525 transition(*this);separateMidiBrowser511=true;viewContextMidi510=true;p.sequenceView=true;captureBrowser510();}

struct BrowserMemory514{std::shared_ptr<BrowserState> state;std::array<BrowserView510,2> views;bool separate=false;std::array<std::shared_ptr<History525>,3> histories;};
static std::shared_ptr<BrowserState> browserForProcessor514(Processor& p,std::shared_ptr<BrowserState> shared,bool initialise){if(shared)return shared;if(initialise&&p.browserMemory514)return std::static_pointer_cast<BrowserMemory514>(p.browserMemory514)->state;return std::make_shared<BrowserState>();}
void retainBrowser514(){if(browserState->target==this)observeHistory525(false,true,true);if(browserState->target==this)captureBrowser510();auto memory=std::make_shared<BrowserMemory514>();memory->state=browserState;memory->views=std::move(browserViews510);memory->separate=separateMidiBrowser511;memory->histories=histories525;p.browserMemory514=std::move(memory);}
