#pragma once

#include "Config.h"
#include "InputSection.h"
#include "OutputSections.h"
#include "SyntheticSections.h"
#include "lld/Common/Memory.h"


namespace lld {
namespace elf {
    class InputSection;
    class InputSectionBase;
    class OutputSection;
    class SymbolTable;
    class JumpTable;

    class Variant {
        std::vector<BaseCommand *> cmd_stack;

        std::string location;

        Expr nextLMA;  // !< LMA Address of Variant Fragment
        Expr addrExpr; // !< VMA Address of Variant
        int fragment_count = 0;

        void placeSection(OutputSection *sec);

        SymbolTable *symtab = nullptr;

        // equal= option => output section, where equal sections should be placed
        std::string osec_equal;
    public:
        Variant(StringRef name)
            : name(name) { }

        StringRef name;

        std::vector<OutputSection *> sections;

        std::vector<BaseCommand *>&& grabCommands() {
            return std::move(cmd_stack); // Destroying read
        }

        enum {
            // Sticky Flags
            JUMP_TABLE_ON_CLASH = (1UL << 0),
            // Clear after each variant
            REMOVE_EXECUTE_FLAG = (1UL << 32),
        };
        uint64_t flags = 0;

        void setEqualOutputSection(StringRef name) { osec_equal = name.str(); }
        
        void startVariant(bool primary);

        void stopVariant(void);

        void addSection(OutputSection *sec);

        void organizeSections(SymbolTable *);

        void dumpSections();
    private:
        typedef std::vector<Defined*> SymbolVector;

        typedef std::vector<InputSection *> InputSectionColumn;

        struct InputSectionTable : public std::vector<std::vector<InputSection *>> {
            unsigned variant_count = 0;
            void reset(unsigned variant_count) {
                this->clear();
                this->variant_count = variant_count;
                for (unsigned v = 0; v < variant_count; v++) {
                    this->emplace_back();
                }
            }

            unsigned cols() const {
                return (*this)[0].size();
            }
            unsigned rows() const { return variant_count; }

            std::pair<bool, int> push_column(const InputSectionColumn &column);
        };

        SymbolVector      duplicates;
        InputSectionTable isec_table;

        bool findDuplicateSymbols(SymbolTable*);
        bool fixDefinedSymbolOrder();

        bool createInputSectionTable();
        bool handleSymbolClashes();

        void createJumpTableEntry(unsigned idx);
        std::vector<JumpTable *> jump_tables;
    };


    struct JumpTable : public SyntheticSection {
        std::vector<std::pair<Defined *, Defined *>> entries;
        OutputSection *osec;
        SymbolTable *symtab;

        JumpTable(OutputSection *osec, SymbolTable *symtab)
            : SyntheticSection(/* flags= */ llvm::ELF::SHF_ALLOC,
                               /* type=  */ llvm::ELF::SHT_PROGBITS,
                               /* align= */ 0x10,
                               ".text.variant_jump_table"),
              osec(osec), symtab(symtab) {}

        virtual void writeTo(uint8_t *buf) override;
        virtual size_t getSize() const override;

        void add_entry(Defined *);
    };
} }

