// Cached musical lengths only. No file opening/decoding from paint, sorting or the message thread.
static int compareBars502(double a,double b){return a==b?0:a<0?1:b<0?-1:a<b?-1:1;}
double browserBarValue502(juce::String path)const{auto it=browserBars->find(path);return it==browserBars->end()?-1:it->second;}
void serviceBrowserBars502(){
 if(browserState->target.getComponent()!=this||presetsView||browserState->barsPending502)return;
 auto original=searchCatalog;auto disk=browserState->diskRevision510;uint64_t revision=LibraryStore::kindRevision("loops.json").load()+LibraryStore::kindRevision("parameter-edits.json").load()+LibraryStore::kindRevision("playback-types.json").load();
 juce::String activePath;double activeBars=-1;{const juce::ScopedTryLock g(p.lock);if(!g.isLocked()||p.editDepth>0)return;if(p.audio&&!p.oneShotMode&&!p.multiMode){activePath=p.audio->path;activeBars=p.loopBeats()/p.sourceBeatsPerBar;}}
 if(browserState->barsSource502==original&&browserState->barsRevision502==revision&&browserState->barsActivePath502==activePath&&browserState->barsActiveValue502==activeBars&&browserState->barsDisk524==disk)return;
 browserState->barsPending502=true;
 juce::StringArray priority524;int first=juce::jmax(0,files.getRowContainingPosition(1,1));int last=juce::jmin(visible.size(),first+files.getHeight()/juce::jmax(1,files.getRowHeight())+2);for(int i=first;i<last;++i)priority524.add(visible[i].getFullPathName());
 auto index=browserState->barsIndex503;auto cancelled=browserState->barsCancelled503;auto safe=browserTarget();int column=browserSortColumn;bool descending=browserSortDescending;
 browserState->barsPool503.addJob([safe,original,revision,activePath,activeBars,column,descending,index,cancelled,disk,priority524]{
  auto lengths=std::make_shared<std::map<juce::String,double>>();std::shared_ptr<std::vector<BrowserSearchRow>> rows;
  try{auto checkpoint=[cancelled]{if(cancelled->load())throw std::runtime_error("Cancelled Bars work");};index->refresh(checkpoint);if(index->disk524!=disk){index->headerBars524.clear();index->disk524=disk;}index->headerBudget524=64;index->headersRemaining524=false;for(auto path:priority524){checkpoint();index->get(path);}int count=0;for(auto& row:*original){if(++count%256==0)checkpoint();double bars=-1;if(row.type!=1&&row.type!=6&&row.type!=7)bars=row.path==activePath?activeBars:index->get(row.path);(*lengths)[row.path]=bars;}
  }catch(const std::exception&){/* Unavailable metadata stays unknown; browsing remains usable. */}
  if(column==5){rows=std::make_shared<std::vector<BrowserSearchRow>>(*original);for(auto& row:*rows){auto it=lengths->find(row.path);row.bars=it==lengths->end()?-1:it->second;}}
  if(column==5)std::sort(rows->begin(),rows->end(),[descending](const auto& a,const auto& b){int n=compareBars502(a.bars,b.bars);if(!n)n=a.name.compareIgnoreCase(b.name);if(!n)n=a.path.compareIgnoreCase(b.path);return descending?n>0:n<0;});
  juce::MessageManager::callAsync([safe,original,rows,revision,column,descending,activePath,activeBars,lengths,disk,remaining=index->headersRemaining524]()mutable{
   if(auto state=safe.state.lock())state->barsPending502=false;if(!safe)return;if(safe->searchCatalog!=original||safe->browserSortColumn!=column||safe->browserSortDescending!=descending)return;
   auto retired=safe->browserBars;safe->browserBars=lengths;safe->browserState->barsPool503.addJob([retired]{});safe->browserState->barsActivePath502=activePath;safe->browserState->barsActiveValue502=activeBars;safe->browserState->barsRevision502=revision;safe->browserState->barsDisk524=disk;safe->browserState->barsSource502=remaining?nullptr:(column==5?rows:original);if(column==5){if(safe->typeFolderSource==original)safe->typeFolderSource=rows;safe->searchCatalog=rows;safe->filterFiles(true);}else safe->files.repaint();
  });
 });
}
