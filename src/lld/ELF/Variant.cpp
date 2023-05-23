#include <iostream>
#include <fstream>
#include <set>

#include "Variant.h"
#include "Symbols.h"
#include "SymbolTable.h"

#undef assert
#define assert_failed(x) do {fprintf(stderr, "%s:%d: %s\n", (const char*)__FILE__, __LINE__, (const char*)x); abort(); } while(0)
#define assert(x) do { if (!(x)) assert_failed(#x); } while(0)

#define DEBUG_TYPE "variant"
//#define DBG(x) do { std::cerr << "variant: " << x << std::endl; } while(0)
#define DBG(x) do{} while(0)

using namespace lld::elf;
using namespace lld;

void Variant::startVariant(bool primary) {
    StringRef StartSymbol;
    if (primary)
        StartSymbol = saver.save("__start_" + name);
    else
        StartSymbol = saver.save("__start_" + name + std::to_string(++fragment_count));

    cmd_stack.push_back(make<SymbolAssignment>(StartSymbol,
                                               [] { return script->getDot(); },
                                               location,
                                               "startVariant"));
    this->nextLMA = [=] {
        return script->getSymbolValue(StartSymbol, location);
    };
    if (primary)
        this->addrExpr = this->nextLMA;
}

void Variant::stopVariant() {
    cmd_stack.push_back(make<SymbolAssignment>(".", nextLMA, location, "stopVariant"));
    // Clear all volatile flags (bit 32-bit 63)
    this->flags &= 0xFFFFFFFF;
}

void Variant::addSection(OutputSection *sec) {

    sec->variant     = this;
    sec->variant_idx = sections.size();
    sec->addrExpr    = this->addrExpr;

    sections.push_back(sec);
    for (BaseCommand *base : sec->sectionCommands)
        if (auto isecd = dyn_cast<InputSectionDescription>(base))
            isecd->variant = this;

    placeSection(sec);
}


void Variant::placeSection(OutputSection *sec) {
    Expr baseLMA  = this->nextLMA;

    SymbolAssignment *load_start = make<SymbolAssignment>(
        saver.save("__load_start_" + sec->name.str()),
        [=] { return ExprValue(sec, false, sec->getLMA() - sec->addrExpr().getValue(), location); },
        location,
        sec->name.str() + "(LMA - addr)");
    cmd_stack.push_back(load_start);

    cmd_stack.push_back(sec);
    // FIXME: sec->lmaExpr  = baseLMA;

    SymbolAssignment *load_stop = make<SymbolAssignment>(
        saver.save("__load_stop_" + sec->name.str()),
        [=] {
            uintptr_t addr = sec->getLMA() - sec->addrExpr().getValue() + sec->size;
            // Remove the addend of the last input section
            for (BaseCommand *base : sec->sectionCommands) {
                if (auto *isecd = dyn_cast<InputSectionDescription>(base)) {
                    if (isecd->sections.size() > 0)
                        addr -= isecd->sections.back()->addend;
                }
            }

            return ExprValue(sec, false,  addr, location);
        }, location,
        " Sum of aligned sections");
    cmd_stack.push_back(load_stop);

    if (sections.size() == 1) {
        Expr moveDot = [=] {
            uint64_t max = 0;
            for (OutputSection *sec : sections)
                max = std::max(max, sec->size);
            return this->addrExpr().getValue() + max;
        };
        cmd_stack.push_back(make<SymbolAssignment>(".", moveDot, location, "max(overlay sizes)"));

        SymbolAssignment *vm_stop = make<SymbolAssignment>(
            saver.save("__stop_" + sec->name.str()),
            [=] {
                // sec->size is updated to the maximum size in
                // LinkerScript::output with sec->addend
                return ExprValue(sec, false, sec->getLMA() - sec->addrExpr().getValue() + sec->size, location);
            }, location,
            "LMA - addr + size");
        cmd_stack.push_back(vm_stop);

        this->nextLMA = moveDot;

    } else {
        this->nextLMA = [=] {
            if (sec->getLMA() == 0)
                return script->getDot();
            return sec->getLMA() + sec->size;
        };

        if (this->flags & REMOVE_EXECUTE_FLAG)
            sec->flags_mask &= ~((uint64_t) llvm::ELF::SHF_EXECINSTR);

        cmd_stack.push_back(make<SymbolAssignment>(".", this->nextLMA, location, "synchronize(" + sec->name.str()+")"));
    }

}

// Adds a column to the isec_table and returns {OK?, col_index}
std::pair<bool, int>
Variant::InputSectionTable::push_column(const InputSectionColumn &column) {
    assert(column.size() == rows());
    // Sanitiy Check this new column
    unsigned valid_values = 0;
    InputSection *isec = nullptr; // Some valid input section
    for (unsigned v = 0; v < variant_count; v++) {
        if (column[v] == nullptr) continue;
        isec = column[v];
        valid_values += 1;
        isec->variant_row = v;
    }

    if (valid_values == 0) {
        return {true, -1};
    } else if (valid_values == 1) {
        DBG("push_column: '" << isec->name.str() << "' occurret exactly once. skip it.");
        // std::string msg = "variant: section " + isec->name.str() + " occured exactly once. skip it";
        // std::cerr << msg << std::endl;
        return {true, -1};
    } else if (valid_values != variant_count) {
        std::string msg = "variant: section " + isec->name.str()
            + " occured more than once but not in every variant";
        error(msg);
        return {false, -1};
    }

    unsigned col_idx = this->cols();

    DBG("push_column: '" <<  isec->name.str() <<  "' add col=" << col_idx);

    // We filled a complete column, append to isec_table
    for (unsigned v = 0; v < variant_count; v++) {
        // Only the first time we encounter an InputSection, we
        // assign it an row,col coordinate
        assert(column[v]->variant_col == -1);
        column[v]->variant_col = col_idx;
        (*this)[v].push_back(column[v]);
        DBG("push_column:  -> " << v << " " << column[v]->name.str());
    }

    return {true, col_idx};
}

bool Variant::findDuplicateSymbols(SymbolTable* symtab) {
    duplicates.clear();
    ////////////////////////////////////////////////////////////////
    // Duplicate Global Symbols!
    // Already chained with ->next by symbol table
    for (Symbol *sym : symtab->symbols()) {
        // Only Defined Symbols with duplicates in our variant
        if (auto *d = dyn_cast<Defined>(sym)) {
            if (!d->next) continue;
            if (!d->section) continue;
            if (auto *isec = dyn_cast<InputSectionBase>(d->section)) {
                if (isec->variant == this) {
                    duplicates.push_back(d);
                }
                // FIXME: This is a replacement for duplicate check
                // that we've disabled in Symbols.cpp. However, this
                // works not yet as symbols from the libgcc.a appear
                // as duplicates here.
                //Duplicate Symbol Definition `__morestack' was not captured by a variant
                //    W0 C0 U0 L0 S0 V55
                //    W0 C0 U0 L0 S0 V55
                //    W0 C0 U0 L0 S0 V55
                //    W0 C0 U0 L0 S0 V55
                #if 0
                if (!d->isWeak() && !d->isCommon() && isec->variant == nullptr) {
                    std::string msg = "Duplicate Symbol Definition `" + d->getName().str() + "' was not captured by a variant";
                    std::cerr << msg << std::endl;
                    while (d) {
                        std::cerr
                            << " W" << d->isWeak() 
                            << " C" << d->isCommon()
                            << " U" << d->isUndefined()
                            << " L" << d->isLazy()
                            << " S" << d->isShared()
                            << " V" << d->value
                            << std::endl;
                        d = d->next;
                    }
                    // return false;
                }
                #endif
            }
        }
    }
    return true;
}


bool Variant::fixDefinedSymbolOrder() {
    unsigned variant_count = sections.size();

    // Step 1.A: Reorder duplicated symbols to point to the symbol
    //           that is defined in the first variant section
    for (unsigned idx = 0; idx < duplicates.size(); idx++) {
        std::vector<Defined *> ordered(variant_count, nullptr);

        unsigned head_var  = -1;     // variant_id of the current head symbol
        unsigned ordered_count =  0; // Number of !nullptr entries in ordered
        for (Defined *p = duplicates[idx]; p; p = p->next) {
            auto *isec = dyn_cast<InputSection>(p->section);
            auto *osec = dyn_cast<OutputSection>(isec->parent);

            if (ordered[osec->variant_idx] != nullptr) {
                if (!p->isWeak() && !isec->originatesFromGroupSection) {
                    std::string msg = "Variant '" + osec->name.str() + "' defines the Symbol '"
                        + toString(*p) + "' multiple times.";
                    error(msg);
                    return false;
                } else {
                    // Symbol is weak and we already have one of these
                    // for this variant. -> Mark it as dead and delete
                    // it later in this function.
                    DBG("drop duplicate section '" << isec->name.str() << "'");
                    isec->markDead();
                    continue;
                }
            }
            // Record the defined symbol
            ordered[osec->variant_idx] = p;
            ordered_count ++;
            // Did we encounter the _real_ h
            if (p == duplicates[idx])
                head_var = osec->variant_idx;
        }

        // If the symbol is not encountered in every variant, we we skip it, if
        // it was encountered exactly once, but signal an error otherwise
        if (ordered_count != variant_count) {
            if (ordered_count == 1) continue;
            std::string msg =
                "variant: duplicate symbol '" +
                duplicates[idx]->getName().str() +
                "' was not defined in all variants";
            for (unsigned vidx = 0; vidx < variant_count; vidx++) {
                if (ordered[vidx] == nullptr) 
                    msg += ". Missing in " + sections[vidx]->name.str();
            }
            error(msg);
            return false;
        }

        ////////////////////////////////////////////////////////////////
        // Problem Setting: We encountered the symbol for variant 2
        // before the variant-0 symbol. Therefore, we swap those
        // symbols by exchaning their memory representation. We use
        // this road, as that memory is also refrenced by the symbol table an others.
        if (head_var != 0) {
            // Make the symbol with variant_idx==0 the head variant.
            // We to this by exchanging the memory
            Defined *D0 = ordered[0], *DH = ordered[head_var];
            Defined tmp = *DH;
            *DH = *D0; *D0 = tmp;
            ordered[0] = DH;
            ordered[head_var] = D0;
            duplicates[idx] = DH;
        }
        // Relink the list in order of the variants.
        for (unsigned idx = 0; idx < variant_count-1; idx++) {
            ordered[idx]->next = ordered[idx+1];
        }
        ordered[variant_count-1]->next = nullptr;
    }

    return true;
}


// The isec_table: Two dimensional table of input sections to align
// Dimension 1: variant_index (0 is the default variant)
// Dimension 2: Sections to Align
bool Variant::createInputSectionTable() {
    bool has_errors = false;
    // Allocate the Section Table
    unsigned variant_count = sections.size();
    isec_table.reset(sections.size());

    // Match the Sections by Section Name
    std::map<StringRef, InputSectionColumn> cols;
    for (unsigned vidx = 0; vidx < variant_count; vidx++) {
        auto osec = sections[vidx];
        for (BaseCommand *base : osec->sectionCommands) {
            if (auto *isecd = dyn_cast<InputSectionDescription>(base)) {
                // Remove all dead sections from input section description. This
                // removed duplicated sections with weak symbols and COMDAT
                // sections, which were marked in fixDefinedSymbolOrder().
                isecd->sections.erase(
                    std::remove_if(
                        isecd->sections.begin(), isecd->sections.end(),
                        [](InputSection * isec){ return !isec->isLive(); }),
                    isecd->sections.end());

                for (auto isec : isecd->sections) {
                    if (cols.find(isec->name) == cols.end()) {
                        cols[isec->name] =
                            InputSectionColumn(variant_count, nullptr);
                    }
                    assert(isec->variant_row == -1);
                    if (cols[isec->name][vidx] != nullptr) {
                        DBG("duplicate section " << isec->name.str());
                        // assert(cols[isec->name][vidx] == nullptr);
                    } else {
                        cols[isec->name][vidx] = isec;
                    }
                }
            }
        }
    }

    // Push collected symbols to our table
    for (auto & kv : cols) {
        auto rc = isec_table.push_column(kv.second);
        if (!rc.first) has_errors = true;
    }

    return !has_errors;
}

bool Variant::handleSymbolClashes() {
    for (unsigned idx = 0; idx < duplicates.size(); idx++) {
        auto *isec0 = dyn_cast<InputSection>(duplicates[idx]->section);
        
        for (Defined *p = duplicates[idx]; p; p = p->next) {
            auto *isecP = dyn_cast<InputSection>(p->section);
            
            bool clash =
                (isec0->variant_col != isecP->variant_col) 
                || (duplicates[idx]->value != p->value);
            if (clash && (flags & JUMP_TABLE_ON_CLASH)) {
                createJumpTableEntry(idx);
                break;
            } else if (clash) {
                std::string msg = "variant: duplicate symbol '" + duplicates[idx]->getName().str()
                    + "' has different section or offsets:\n";
                for (Defined *p = duplicates[idx]; p; p = p->next) {
                    auto *isec = dyn_cast<InputSection>(p->section);
                    msg += " Symbol `" + p->getName().str() + "' has offset " + Twine(p->value).str() + " in section `"
                        + isec->name.str() + "'\n";
                }
                error(msg);
                return true;
            }
        }
    }
    return true;
}

void JumpTable::add_entry(Defined *sym_orig) {
    StringRef name = saver.save(sym_orig->getName().str() + ".private." + osec->name.str());
    Defined *sym_new = (Defined*) symtab->addSymbol(
        Defined(nullptr, saver.save(name), llvm::ELF::STB_GLOBAL,
                llvm::ELF::STV_DEFAULT, llvm::ELF::STT_FUNC,
                sym_orig->value, sym_orig->size, sym_orig->section));

    sym_orig->section = this;
    sym_orig->value   = entries.size() * 5;
    sym_orig->size    = 5;

    entries.push_back(std::make_pair(sym_orig, sym_new));
}

using llvm::support::endian::write32le;

void JumpTable::writeTo(uint8_t *buf) {
    for (unsigned slot = 0; slot < entries.size(); slot++) {
        Defined *sym_new = entries[slot].second;
        uint8_t insn[] = {
            0xe9, 0, 0, 0, 0 // X86 Jump
        };
        auto target = sym_new->section->getVA(sym_new->value);
        auto source = this->getVA(slot * sizeof(insn));

        write32le(insn+1, target - (source + 5));

        memcpy(buf, insn, sizeof(insn));
        buf += sizeof(insn);
    }
}
size_t JumpTable::getSize() const {
    return entries.size() * 5;
}

void Variant::createJumpTableEntry(unsigned idx) {
    Defined *symbol = duplicates[idx];
    duplicates[idx] = nullptr; // Handled by Jump Table

    if (jump_tables.size() == 0) {
        // Create Jump Table Sections. One Section for each variant
        for (unsigned i = 0; i < sections.size(); i++) {
            jump_tables.push_back(new JumpTable(sections[i], symtab));
        }
    }

    for (Defined *p = symbol; p; p = p->next) {
        auto *isec = dyn_cast<InputSection>(p->section);
        auto *osec = dyn_cast<OutputSection>(isec->parent);
        jump_tables[osec->variant_idx]->add_entry(p);
    }
}

void Variant::organizeSections(SymbolTable* symtab) {
    unsigned variant_count = sections.size();
    this->symtab = symtab;

    // 1. We find all duplicate symbols
    if (!findDuplicateSymbols(symtab)) {
        error("variant: findDuplicateSymbols failed");
        return;
    }
    if (!fixDefinedSymbolOrder()) {
        error("variant: fixDefinedSymbolOrder failed");
        return;
    }
    if (!createInputSectionTable()) {
        error("variant: createInputSectionTable failed");
        return;
    }
    if (!handleSymbolClashes()) {
        error("variant: handleSymbolClashes failed");
        return;
    }

    // Merge and move equal input sections
    if (this->osec_equal.size() > 0) {
        OutputSection *osec_equal = script->getOutputSection(this->osec_equal);
        if (osec_equal == nullptr) {
            error("variant: equal output section `" + this->osec_equal + "' was not found");
            return;
        }
        InputSectionDescription *isecd;
        for (BaseCommand *base : osec_equal->sectionCommands)
            if ((isecd = dyn_cast<InputSectionDescription>(base)))
                break;
        if (isecd == nullptr) {
            error("variant: equal output section `" + this->osec_equal + "' has no input section description");
            return;
        }

        for (unsigned idx = 0; idx < isec_table[0].size(); idx++) {
            if (!isec_table[0][idx]) continue;
            auto isec = isec_table[0][idx];
            bool equal = true;
            for (unsigned v = 1; v < variant_count; v++) {
                assert(isec != nullptr && isec_table[v][idx] != nullptr);
                if (isec->data() != isec_table[v][idx]->data()) {
                equal = false;
                break;
                }
            }
            if (equal) {
                std::cout << "Move equal sections: " << isec->name.str()
                          << " to " << this->osec_equal <<  std::endl;
                // Move to equal= output section
                isecd->sections.push_back(isec);
                isec->parent = osec_equal;
                // Remove from isec_table to avoid aligned placement
                for (unsigned v = 0; v < variant_count; v++) {
                    isec_table[v][idx] = nullptr;
                }
                // Remove from original isecd is done as variant_row is still set.
            }
        }
    }

    // Make all sections within the same column of equal length by setting
    // isec->addend
    for (unsigned idx = 0; idx < isec_table[0].size(); idx++) {
        if (!isec_table[0][idx]) continue;

        uint64_t max_size = 0;
        for (unsigned variant_idx = 0; variant_idx < variant_count; variant_idx++) {
            assert(isec_table[variant_idx][idx] != nullptr);
            max_size = std::max(max_size, isec_table[variant_idx][idx]->getSize());
        }
        for (unsigned variant_idx = 0; variant_idx < variant_count; variant_idx++) {
            auto *isec = isec_table[variant_idx][idx];
            assert(isec != nullptr);
            isec->addend = max_size - isec->getSize();
        }
    }

    std::ofstream var_file("variant." + name.str());

    // Create one InputSection Description per Variant
    for (unsigned variant_idx = 0; variant_idx < variant_count; variant_idx++) {
        // Create an InputSectionDescription and prepend it to the respective output section
        auto osec = sections[variant_idx];
        auto *cmd = make<InputSectionDescription>("*", 0, 0);
        var_file << osec->name.str() << " {\n";

        // Add Jump Table Section
        if (jump_tables.size() > 0) {
            jump_tables[variant_idx]->parent = osec;
            cmd->sections.push_back(jump_tables[variant_idx]);
            for (auto &syms : jump_tables[variant_idx]->entries) {
                var_file << "   J Sym(" << syms.first->getName().str() <<") -> "
                         << "Sec(" << syms.second->section->name.str()
                         << "," << syms.second->value << ")"
                         << std::endl;
            }
        }

        std::multimap<int64_t, InputSection*> filler_sections;
        for (BaseCommand *base : osec->sectionCommands) {
            if (auto *isecd = dyn_cast<InputSectionDescription>(base)) {
                for (auto isec : isecd->sections) {
                    assert(isec != nullptr);
                    // All sections that were not assigned a column
                    // are potential fillers
                    if (isec->getSize() > 0 && isec->variant_col == -1) {
                        unsigned size = isec->getSize() + isec->alignment - 1;
                        filler_sections.insert({-1 * (int64_t)size, isec});
                    }
                }
            }
        }

        // Add the aligned sections in order of the isec_table
        for (unsigned idx = 0; idx < isec_table[variant_idx].size(); idx++) {
            if (auto *isec = isec_table[variant_idx][idx]) {
                if (isec == nullptr) continue;
                cmd->sections.push_back(isec);

                var_file << "   " << idx << " Sec(" << isec->name.str() <<")"
                         << " size=" << isec->getSize()
                         << " align=" << isec->alignment
                         << " addend=" << isec->addend
                         << std::endl;

                // Fill the gap greedy
                while(isec->addend > 0) {
                    int64_t gap = (int64_t)isec->addend;
                    auto it = filler_sections.upper_bound(-1 * gap);
                    if (it == filler_sections.end())
                        break;
                    auto filler = it->second;

                    if (isec->alignment != filler->alignment)
                        break;

                    assert(filler != nullptr);
                    var_file << "   ^ Sec(" << filler->name.str() <<")"
                             << " size=" << filler->getSize()
                             << " align=" << filler->alignment
                             << " addend=" << filler->addend
                             << std::endl;
                    assert(filler->getSize() <= isec->addend);
                    filler_sections.erase(it);
                    // We mark the filler section as part of the
                    // variant table by setting its index.
                    filler->variant_row = variant_idx;
                    filler->variant_col = idx;

                    cmd->sections.push_back(filler);

                    auto align = filler->alignment;
                    assert(filler);
                    isec->addend   = (align - (isec->getSize() % align));
                    filler->addend = (gap - isec->addend - filler->getSize());
                    isec = filler;
                }
            }

        }

        // Remove moved sections from the following input section
        // descriptions.
        for (BaseCommand *base : osec->sectionCommands) {
            if (auto *isecd = dyn_cast<InputSectionDescription>(base)){
                auto rptr = isecd->sections.begin();
                auto wptr = isecd->sections.begin();

                while (rptr != isecd->sections.end()) {
                    if ((*rptr)->variant_col == -1) {
                        auto &isec = *rptr;
                        assert(isec != nullptr);
                        var_file << "   ? Sec(" << isec->name.str() <<")"
                                 << " size=" << isec->getSize()
                                 << " align=" << isec->alignment
                                 << " addend=" << isec->addend
                                 << std::endl;
                        *wptr++ = *rptr++;
                    } else {
                        rptr++;
                    }
                }
                isecd->sections.erase(wptr, isecd->sections.end());
            }
        }

        var_file << "}\n";

        // Insert our dummy input description section at the beginning of the variant
        osec->sectionCommands.insert(osec->sectionCommands.begin(), cmd);
    }
    // dumpSections();
}

void Variant::dumpSections() {
    for (OutputSection *os : sections) {
        std::cout << " OSEC " << os->name.str()  << std::endl;
        for (BaseCommand *base : os->sectionCommands) {
            if (auto cmd = dyn_cast<SymbolAssignment>(base)) {
                std::cout << " Sym " << cmd->name.str() << std::endl;
            }
            if (auto isecd = dyn_cast<InputSectionDescription>(base)){
                std::cout << "  ISecD " << isecd << std::endl;
                for (auto isec : isecd->sections) {
                    std::cout << "    ISec " << isec->name.str() << " " << isec->getSize() << "+" << isec->addend << std::endl;

                    // Search all the symbols in the file of the section
                    // and find out a Defined symbol with name that is within the section.
                    if (!isec->file)
                        continue;
                    for (Symbol *sym : isec->file->getSymbols())
                        if (!sym->isSection() && !sym->isLocal()) // Filter out section-type symbols here.
                            if (auto *d = dyn_cast<Defined>(sym))
                                if (isec == d->section)
                                    std::cout << "      Sym " << d->getName().str() << " " << (void*)d << toString(d->file) << "\n";
                }
            }
        }
    }
}

