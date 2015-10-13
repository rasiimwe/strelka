// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Starka
// Copyright (c) 2009-2014 Illumina, Inc.
//
// This software is provided under the terms and conditions of the
// Illumina Open Source Software License 1.
//
// You should have received a copy of the Illumina Open Source
// Software License 1 along with this program. If not, see
// <https://github.com/sequencing/licenses/>
//

///
/// \author Chris Saunders
///

#pragma once


#include "codon_phaser.hh"
#include "gvcf_block_site_record.hh"
#include "gvcf_locus_info.hh"
#include "gvcf_compressor.hh"
#include <iosfwd>
#include "variant_prefilter_stage.hh"

class calibration_models;


///
/// Assembles all site and indel call information into a consistent set, blocks output
/// and writes to a VCF stream
///
struct gvcf_writer : public variant_pipe_stage_base
{
    gvcf_writer(
        const starling_options& opt,
        const starling_deriv_options& dopt,
        const reference_contig_segment& ref,
        const RegionTracker& nocompress_regions,
        std::ostream* os,
        const calibration_models& cm);


    void process(std::unique_ptr<site_info>) override;
    void process(std::unique_ptr<indel_info>) override;
    void flush() override;


private:
    void add_site_internal(digt_site_info& si);
    void add_site_internal(continuous_site_info& si);
    void write_block_site_record();

    // queue site record for writing, after
    // possibly joining it into a compressed non-variant block
    //
    template<class TSiteInfo>
    void queue_site_record(const TSiteInfo& si)
    {
        //test for basic blocking criteria
        if (! _gvcf_comp.is_site_compressable(si))
        {
            write_block_site_record();
            write_site_record(si);
            return;
        }

        if (! _block.test(si))
        {
            write_block_site_record();
        }
        _block.join(si);
    }


    void write_indel_record(const digt_indel_info& ii);
    void write_indel_record(const continuous_indel_info& ii);

    void write_site_record(const continuous_site_info& si) const;
    void write_site_record(const gvcf_block_site_record& si) const;
    void write_site_record(const digt_site_info& si) const;


    /// fill in missing sites
    void skip_to_pos(const pos_t target_pos);

    const digt_site_info& get_empty_site(const pos_t pos)
    {
        _empty_site.pos = pos;
        _empty_site.ref = _ref.get_base(pos);
        _empty_site.Unphasable = true;
        return _empty_site;
    }

    const starling_options& _opt;
    const known_pos_range _report_range;
    const reference_contig_segment& _ref;
    std::ostream* _osptr;
    const char* _chrom;
    const gvcf_deriv_options _dopt;
    gvcf_block_site_record _block;
    pos_t _head_pos;
    digt_site_info _empty_site;

    std::unique_ptr<digt_indel_info> _last_indel;

    void filter_site_by_last_indel_overlap(digt_site_info& si);

    gvcf_compressor _gvcf_comp;
    const calibration_models& _CM;




};
