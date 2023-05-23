#include "MultivariantLinking.h"
#include "Config.h"
#include "LinkerScript.h"
#include "SymbolTable.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "lld/Common/Memory.h"
#include <algorithm>

using lld::elf::script;
using lld::elf::config;

namespace MVL {
  const uint32_t PT_MVS = 1337;
  const std::string META_SECTION_NAME(".genmeta");
  const std::string MVS_PREFIX(".gen");
  static std::vector<std::vector<InputSectionBase *>*> functionGenerations;
  static std::vector<OutputSection*> multivariantOutputSections;

  struct MultivariantMeta {
    size_t sizeStruct;  // The overall size of this whole struct.
    size_t sizeSegment; // Size of a segment. All segments share the same size.
    size_t numSegments; // Number of all configuration segments.
    uint64_t virtualAddress; // Virtual address of the overlay segments.
    uint64_t* viewFileOffsets; // All file offsets of all views.
  };

  
  void fillMetaSection(){
    if(!config->multivariantLinking || multivariantOutputSections.size() == 0)
      return;
    
    OutputSection *metaSection = script->getOrCreateOutputSection(llvm::StringRef(META_SECTION_NAME));
    OutputSection *multivariantOutputSection = multivariantOutputSections.front();
    size_t sizeSegment = multivariantOutputSection->size;
    size_t structSize = sizeof(MultivariantMeta::sizeStruct)  + sizeof(MultivariantMeta::sizeSegment) +
                        sizeof(MultivariantMeta::numSegments) + sizeof(MultivariantMeta::virtualAddress) +
                        sizeof(*MultivariantMeta::viewFileOffsets) * functionGenerations.size();
    // Allocate enough space to save the whole struct.
    MultivariantMeta* metaData = (MultivariantMeta*)(new (lld::getSpecificAllocSingleton<uint8_t>().Allocate(structSize)) uint8_t());
    // All segments share the same size.
    // They all contain one output section all having the same size.
    metaData->sizeSegment = sizeSegment;
    metaData->sizeStruct =  structSize;
    metaData->numSegments = functionGenerations.size();
    metaData->virtualAddress = multivariantOutputSection->getVA();
    // The memory is directly behind the four fields.
    uint64_t* segmentIdentifiers = (uint64_t*)((uint64_t)metaData + sizeof(MultivariantMeta::sizeStruct) +
                                    sizeof(MultivariantMeta::sizeSegment) + sizeof(MultivariantMeta::virtualAddress) +
                                    sizeof(MultivariantMeta::numSegments));
    for(auto outputSection: multivariantOutputSections)
      *(segmentIdentifiers++) = outputSection->offset;
      
    
    // Add the new section containing the real data and remove the old dummy section.
    lld::elf::InputSection* metaDataSection = lld::make<lld::elf::InputSection>(
      nullptr, llvm::ELF::SHF_ALLOC, llvm::ELF::SHT_PROGBITS, 0x10,
      llvm::makeArrayRef<uint8_t>((uint8_t*)metaData, structSize),
      llvm::StringRef(META_SECTION_NAME)
    );
    metaSection->sectionCommands.clear();
    metaSection->recordSection(metaDataSection);
    metaSection->finalizeInputSections();
    script->sectionCommands.push_back(metaSection);
  }

  static bool isMultivariantFunction(const InputSectionBase *isec){
    // This function is a multivariant function if we recorded it and found at least two variants of it.
    auto generationHasFunction = [isec](const InputSectionBase* isb){ return isec->name.str() == isb->name.str();};
    return std::count_if(
      functionGenerations.begin(),
      functionGenerations.end(),
      [isec, generationHasFunction](const std::vector<InputSectionBase*>* generation){
        return std::find_if(generation->begin(), generation->end(), generationHasFunction) != generation->end();
      }
    ) >= 2;
  }

  OutputSection *getMultivariantSection(const InputSectionBase *isec){
    if(!config->multivariantLinking)
      return nullptr;

    // Try to find a matching input section for this multivariant.
    // If there is one just return its output section since they shall share that one.
    if(isMultivariantFunction(isec)){
      for(auto generation: functionGenerations){
        auto variant = std::find_if(
          generation->begin(),
          generation->end(),
          [isec](const InputSectionBase* isb){ return isec->name.str() == isb->name.str();}
        );
        if(variant != generation->end()){
          // The parent is always registered as an OutputSection.
          return llvm::cast<OutputSection>((*variant)->parent);
        }
      }
    }

    return nullptr;
  }
  size_t getMultivariantSectionMaxSize(const InputSectionBase *isec){
    if(!config->multivariantLinking)
      return 0;

    if(!isMultivariantFunction(isec))
      return 0;

    size_t maxFunctionSize = 0;
    for(auto generation: functionGenerations){
      auto variant = std::find_if(
          generation->begin(),
          generation->end(),
          [isec](const InputSectionBase* isb){ return isec->name.str() == isb->name.str();}
        );
      if(variant != generation->end())
        maxFunctionSize = (*variant)->getSize() > maxFunctionSize ? (*variant)->getSize() : maxFunctionSize;
    }

    return maxFunctionSize;
  }

  std::vector<OutputSection*> getMultivariantGenerations(){
    if(!config->multivariantLinking)
      return {};
      
    return multivariantOutputSections;
  }

  uint64_t getOverlayMultivariantSectionVA(const OutputSection *osec){
    if(!config->multivariantLinking)
      return 0;
      
    // Iterate over all output sections we got and check if any of them already got a VA.
    // If this is the case we return it because we need that overlay.
    for(auto os: multivariantOutputSections){
        uint64_t va = os->getVA();
        if(va > 0)
          return va;
    }
    return 0;
  }

  void addMultivariantSegmentHeaders(std::vector<PhdrEntry *>& existingHeaders){
    auto placeInDefaultSegment = [](const OutputSection* osec){
        llvm::StringRef& defaultMultivariantObjectFile = std::find_if(
          config->multivariantObjects.begin(),
          config->multivariantObjects.end(),
          [](const std::pair<int, llvm::StringRef> &object){ return object.first == 0;}
        )->second;
        // All input sections of the multivariant output section are out of the same object file.
        InputSection* firstInputSection = lld::elf::getFirstInputSection(osec);
        return firstInputSection && firstInputSection->file && firstInputSection->file->getName() == defaultMultivariantObjectFile;
    };
    
    using llvm::ELF::PT_LOAD;
    std::vector<PhdrEntry *> segmentHeaders;
    auto addHdr = [&](unsigned type, unsigned flags) -> PhdrEntry * {
      segmentHeaders.push_back(lld::make<PhdrEntry>(type, flags));
      return segmentHeaders.back();
    };

    for(OutputSection *osec: MVL::getMultivariantGenerations()){
      PhdrEntry *genHdr = nullptr;
      if(placeInDefaultSegment(osec)){
        genHdr = addHdr(PT_LOAD, osec->getPhdrFlags());
      } else {
        genHdr = addHdr(MVL::PT_MVS, osec->getPhdrFlags());
        // We page-align this segment the same way we do with normal PT_LOADs.
        genHdr->p_align = config->maxPageSize;
      }
      genHdr->add(osec);
    }
    
    existingHeaders.insert(existingHeaders.end(), segmentHeaders.begin(), segmentHeaders.end());
  }

  bool isMultivariantOutputSection(const OutputSection *osec){
    if(!config->multivariantLinking)
      return false;

    return std::find_if(multivariantOutputSections.begin(), multivariantOutputSections.end(), [osec](const OutputSection* os){
      return osec == os;
    }) != multivariantOutputSections.end();
  }

  void processMultivariantSections(){
    if(!config->multivariantLinking || functionGenerations.size() == 0)
      return;

    // Create the meta section first so that the linker automatically places it within a readonly segment.
    OutputSection *meta = script->getOrCreateOutputSection(llvm::StringRef(META_SECTION_NAME));
    // So now we are a little bit hacky.
    // We can already calculate the maximum size needed for the meta output section, but the data ifself is not ready yet.
    // But since further linker steps require the size just NOW we create a dummy input section to mimic existing data.
    // Later on we replace this dummy section by the real one having the data in itself.
    size_t segmentSize = getMultivariantSectionMaxSize(functionGenerations.front()->front());
    size_t segmentCount = functionGenerations.size();
    size_t structSize = sizeof(segmentSize) + sizeof(segmentCount) + sizeof(uint64_t) * segmentCount + sizeof(size_t) + sizeof(MultivariantMeta::virtualAddress);
    lld::elf::InputSection *isec = lld::make<lld::elf::InputSection>(
      nullptr, llvm::ELF::SHF_ALLOC, llvm::ELF::SHT_PROGBITS, 0x10,
      llvm::makeArrayRef<uint8_t>(nullptr, structSize),
      llvm::StringRef(META_SECTION_NAME)
    );
    // The default partition for every section is 1. We don't make use of llvm's partition feature.
    isec->partition = 1;
    meta->recordSection(isec);
    script->sectionCommands.push_back(meta);

    lld::elf::Symbol *sym = lld::elf::symtab->addSymbol(lld::elf::Defined{/*file=*/nullptr, "genmeta_start", llvm::ELF::STB_GLOBAL,
                          llvm::ELF::STV_DEFAULT, llvm::ELF::STT_OBJECT,
                          /*value=*/0, /*size=*/structSize, meta});
    sym->exportDynamic = 1;

    // For each generation create an output section and insert the input sections.
    size_t generationCounter = 0;
    for(auto generation: functionGenerations){
      std::string* sectionName = lld::make<std::string>(MVS_PREFIX); sectionName->append(std::to_string(generationCounter));
      OutputSection *sec = script->getOrCreateOutputSection(llvm::StringRef(*sectionName));
      sec->alignment = 0x1000;
      sec->inOverlay = true;
      for(auto variant: *generation){
        if(isMultivariantFunction(variant))
          sec->recordSection(variant);
      }

      script->sectionCommands.push_back(sec);
      multivariantOutputSections.emplace_back(sec);
      ++generationCounter;
    }
  }

  void recordMultivariantSection(InputSectionBase *isec){
    if(!config->multivariantLinking)
      return;

    // The following sections are ignored because they are allowed to occur more than once.
    // However they are no multiariant methods.
    std::vector<std::string> ignoredSections = {
      {".text.hot"},
      {".text.unlikely"}
    };
    bool isIgnoredSection = std::any_of(
      ignoredSections.begin(),
      ignoredSections.end(),
      [isec](const std::string& ignoredSectionName){
        return isec->name.str().find(ignoredSectionName) != std::string::npos;
      }
    );
    bool isInMultivariantObjectFile = std::any_of(
      config->multivariantObjects.begin(),
      config->multivariantObjects.end(),
      [isec](const std::pair<int, llvm::StringRef>& object){
        return isec->file && isec->file->getName() == object.second;
      }
    );
    if(isec->name.str().find(".text.") != std::string::npos && !isIgnoredSection && isInMultivariantObjectFile){
      bool functionVariantRegistered = false;
      for(auto generation: functionGenerations){
        // Variant not already placed within that generation. Add it.
        if(std::find_if(
            generation->begin(),
            generation->end(),
            [isec](const InputSectionBase* isb){ return isec->name.str() == isb->name.str();}
          ) == generation->end()
        ){
          generation->push_back(isec);
          functionVariantRegistered = true;
          break;
        }
      }
      // Not registered. Create new vector and add variant.
      if(!functionVariantRegistered){
        std::vector<InputSectionBase*>* new_vector = new std::vector<InputSectionBase*>;
        new_vector->push_back(isec);
        functionGenerations.push_back(new_vector);
      }
    }
  }


static std::vector<std::pair<int, Symbol*>> multivariantLocalSymbols;
void recordMultivariantLocalSymbol(Symbol *sym){
    if(!config->multivariantLinking)
      return;

    if(!sym->isLocal())
      return;
    

    auto multivariantObjectOfSymbol = std::find_if(
      config->multivariantObjects.begin(),
      config->multivariantObjects.end(),
      [sym](const std::pair<int, llvm::StringRef> &object){ return sym->file->getName() == object.second;}
    );
    if(multivariantObjectOfSymbol != config->multivariantObjects.end())
      multivariantLocalSymbols.emplace_back(std::make_pair(multivariantObjectOfSymbol->first, sym));
  }

  SectionBase *getMultivariantLocalSymbolSectionBase(const Symbol *sym){
    if(!config->multivariantLinking)
      return nullptr;

    bool isMultivariantLocalSymbol = sym->isLocal() && std::any_of(
      config->multivariantObjects.begin(),
      config->multivariantObjects.end(),
      [sym](const std::pair<int, llvm::StringRef> &object){ return sym->file->getName() == object.second;}
    );
    if(!isMultivariantLocalSymbol)
      return nullptr;
    
    auto symFound = std::find_if(
      multivariantLocalSymbols.begin(),
      multivariantLocalSymbols.end(),
      [sym](const std::pair<int, Symbol *> symbol){
        Symbol *s = symbol.second;
        return 
          symbol.first == 0 && // Always return symbols relative to the first default variant
          s->getName() == sym->getName() &&
          s->isDefined() && sym->isDefined() &&
          s->binding == sym->binding && s->type == sym->type &&
          s->getSize() == sym->getSize() && s->stOther == sym->stOther &&
          llvm::cast<lld::elf::Defined>(sym)->section &&
          llvm::cast<lld::elf::Defined>(s)->section &&
          (llvm::cast<lld::elf::Defined>(sym)->section->name.contains(".bss") || llvm::cast<lld::elf::Defined>(sym)->section->name.contains(".data") || llvm::cast<lld::elf::Defined>(sym)->section->name.contains(".rodata")) &&
          llvm::cast<lld::elf::Defined>(sym)->section->name == llvm::cast<lld::elf::Defined>(s)->section->name &&
          llvm::cast<lld::elf::Defined>(sym)->value == llvm::cast<lld::elf::Defined>(s)->value;
      }
    );
    if(symFound != multivariantLocalSymbols.end())
      return llvm::cast<lld::elf::Defined>(symFound->second)->section;
    else
      return nullptr;
  }
}
