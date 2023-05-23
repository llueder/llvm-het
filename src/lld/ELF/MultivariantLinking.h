#pragma once

#include "Config.h"
#include "InputSection.h"
#include "OutputSections.h"

using lld::elf::InputSectionBase;
using lld::elf::InputSection;
using lld::elf::OutputSection;
using lld::elf::PhdrEntry;
using lld::elf::SectionBase;
using lld::elf::Symbol;

namespace MVL{
    std::vector<OutputSection*> getMultivariantGenerations();
    OutputSection *getMultivariantSection(const InputSectionBase *isec);
    SectionBase *getMultivariantLocalSymbolSectionBase(const Symbol* sym);
    uint64_t getOverlayMultivariantSectionVA(const OutputSection *osec);
    size_t getMultivariantSectionMaxSize(const InputSectionBase *isec);
    void addMultivariantSegmentHeaders(std::vector<PhdrEntry *>& existingHeaders);
    bool isMultivariantOutputSection(const OutputSection *osec);
    void multivariantSectionComputed(const OutputSection *osec);
    void fillMetaSection();
    void processMultivariantSections();
    void recordMultivariantSection(InputSectionBase *isec);
    void recordMultivariantLocalSymbol(Symbol *sym);

    extern const uint32_t PT_MVS;
    extern const std::string META_SECTION_NAME;
    extern const std::string MVS_PREFIX;
}