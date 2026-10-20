#pragma once
namespace DestructiveRender {
inline juce::Result trim(juce::File file,const AudioData& source,int begin,int end,std::shared_ptr<AudioData>* output=nullptr){
 if(!LibraryStore::editable(file))return juce::Result::fail("Shared library is read-only");
 if(!source.contentHash.isEmpty()&&LibraryStore::fingerprint(file)!=source.contentHash)return juce::Result::fail("The source file changed since loading. Reload it before rendering.");
 if(end<=begin||!file.existsAsFile())return juce::Result::fail("No valid loop region");
 juce::AudioFormatManager formats;formats.registerBasicFormats();auto* format=formats.findFormatForFileExtension(file.getFileExtension());
 if(!format||!file.hasFileExtension("wav;aif;aiff;flac"))return juce::Result::fail("Overwrite Render supports WAV, AIFF and FLAC. Compressed MP3/M4A sources cannot be overwritten by this encoder.");
 std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));if(!reader)return juce::Result::fail("Cannot read source format");int bits=reader->bitsPerSample;bool floating=reader->usesFloatingPointData;reader.reset();
 juce::AudioBuffer<float> trimmed(source.buffer.getNumChannels(),end-begin);trimmed.clear();int first=juce::jmax(0,begin),last=juce::jmin(end,source.buffer.getNumSamples());if(last>first)for(int ch=0;ch<trimmed.getNumChannels();++ch)trimmed.copyFrom(ch,first-begin,source.buffer,ch,first,last-first);
 juce::TemporaryFile temporary(file);std::unique_ptr<juce::OutputStream> stream=temporary.getFile().createOutputStream();if(!stream)return juce::Result::fail("Cannot create temporary render");
 auto options=juce::AudioFormatWriterOptions().withSampleRate(source.rate).withNumChannels(trimmed.getNumChannels()).withBitsPerSample(bits).withSampleFormat(floating?juce::AudioFormatWriterOptions::SampleFormat::floatingPoint:juce::AudioFormatWriterOptions::SampleFormat::integral);
 auto writer=format->createWriterFor(stream,options);if(!writer||!writer->writeFromAudioSampleBuffer(trimmed,0,trimmed.getNumSamples()))return juce::Result::fail("Render failed; source unchanged");writer.reset();
 if(!temporary.overwriteTargetFileWithTemporary())return juce::Result::fail("Could not replace source");
 if(output){auto data=std::make_shared<AudioData>();data->buffer=std::move(trimmed);data->rate=source.rate;data->path=file.getFullPathName();data->contentHash=LibraryStore::fingerprint(file);data->analysedTiming=source.analysedTiming;data->analysedTiming.beats=(end-begin)/source.rate*data->analysedTiming.bpm/60;data->analysedPreferred=source.analysedPreferred;data->analysedMeter=source.analysedMeter;data->buildWaveformPeaks();data->peaks.resize((data->buffer.getNumSamples()+AudioData::peakStride-1)/AudioData::peakStride);for(int n=0;n<data->buffer.getNumSamples();++n)for(int ch=0;ch<data->buffer.getNumChannels();++ch)data->peaks[n/AudioData::peakStride]=juce::jmax(data->peaks[n/AudioData::peakStride],std::abs(data->buffer.getSample(ch,n)));for(auto point:source.transients)if(point>=begin&&point<end)data->transients.push_back(point-begin);data->integratedLufs=LoopLoudness::analyse(file,data->buffer,data->rate);*output=data;}
 return juce::Result::ok();
}
}
