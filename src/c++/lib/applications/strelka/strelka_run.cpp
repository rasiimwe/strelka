// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Strelka - Small Variant Caller
// Copyright (c) 2009-2016 Illumina, Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//

///
/// \author Chris Saunders
///

#include "strelka_pos_processor.hh"
#include "strelka_streams.hh"
#include "strelka_run.hh"

#include "appstats/RunStatsManager.hh"
#include "blt_util/log.hh"
#include "common/Exceptions.hh"
#include "htsapi/bam_header_info.hh"
#include "starling_common/HtsMergeStreamerUtil.hh"
#include "starling_common/starling_ref_seq.hh"
#include "starling_common/starling_pos_processor_util.hh"



namespace INPUT_TYPE
{
enum index_t
{
    CANDIDATE_INDELS,
    FORCED_GT_VARIANTS,
    NOISE_VARIANTS,
    PLOIDY_REGION,
    NOCOMPRESS_REGION
};
}



void
strelka_run(
    const prog_info& pinfo,
    const strelka_options& opt)
{
    using namespace illumina::common;

    // ensure that this object is created first for runtime benchmark
    RunStatsManager segmentStatMan(opt.segmentStatsFilename);

    opt.validate();

    const strelka_deriv_options dopt(opt);
    const StrelkaSampleSetSummary ssi;
    starling_read_counts brc;
    reference_contig_segment ref;

    ////////////////////////////////////////
    // setup streamData:
    //
    HtsMergeStreamer streamData;

    // additional data structures required in the region loop below, which are filled in as a side effect of
    // streamData initialization:
    std::vector<std::reference_wrapper<const bam_hdr_t>> bamHeaders;
    {
        std::vector<unsigned> registrationIndices;
        for (const bool isTumor : opt.alignFileOpt.isAlignmentTumor)
        {
            const unsigned rindex(isTumor ? STRELKA_SAMPLE_TYPE::TUMOR : STRELKA_SAMPLE_TYPE::NORMAL);
            registrationIndices.push_back(rindex);
        }

        bamHeaders = registerAlignments(opt.alignFileOpt.alignmentFilename, registrationIndices, streamData);

        assert(not bamHeaders.empty());
        const bam_hdr_t& referenceHeader(bamHeaders.front());

        static const bool noRequireNormalized(false);
        registerVcfList(opt.input_candidate_indel_vcf, INPUT_TYPE::CANDIDATE_INDELS, referenceHeader, streamData,
                        noRequireNormalized);
        registerVcfList(opt.force_output_vcf, INPUT_TYPE::FORCED_GT_VARIANTS, referenceHeader, streamData);

        registerVcfList(opt.noise_vcf, INPUT_TYPE::NOISE_VARIANTS, referenceHeader, streamData);
    }

    const bam_hdr_t& referenceHeader(bamHeaders.front());
    const bam_header_info referenceHeaderInfo(referenceHeader);

    strelka_streams client_io(opt, dopt, pinfo, referenceHeader, ssi);
    strelka_pos_processor sppr(opt, dopt, ref, client_io);

    // parse and sanity check regions
    const auto& referenceAlignmentFilename(opt.alignFileOpt.alignmentFilename.front());
    std::vector<AnalysisRegionInfo> regionInfo;
    getStrelkaAnalysisRegions(opt, referenceAlignmentFilename, referenceHeaderInfo, regionInfo);

    for (const auto& rinfo : regionInfo)
    {
        sppr.resetRegion(rinfo.regionChrom, rinfo.regionRange);
        streamData.resetRegion(rinfo.streamerRegion.c_str());
        setRefSegment(opt, rinfo.regionChrom, rinfo.refRegionRange, ref);

        while (streamData.next())
        {
            const pos_t currentPos(streamData.getCurrentPos());
            const HTS_TYPE::index_t currentHtsType(streamData.getCurrentType());
            const unsigned currentIndex(streamData.getCurrentIndex());

            // wind sppr forward to position behind buffer head:
            sppr.set_head_pos(currentPos - 1);

            if (HTS_TYPE::BAM == currentHtsType)
            {
                // Remove the filter below because it's not valid for
                // RNA-Seq case, reads should be selected for the report
                // range by the bam reading functions
                //
                // /// get potential bounds of the read based only on current_pos:
                // const known_pos_range any_read_bounds(current_pos-max_indel_size,current_pos+MAX_READ_SIZE+max_indel_size);
                // if( sppr.is_range_outside_report_influence_zone(any_read_bounds) ) continue;

                // Approximate begin range filter: (removed for RNA-Seq)
                //if((current_pos+MAX_READ_SIZE+MAX_INDEL_SIZE) <= rlimit.begin_pos) continue;
                processInputReadAlignment(opt, ref, streamData.getCurrentBamStreamer(),
                                          streamData.getCurrentBam(), currentPos,
                                          brc, sppr, currentIndex);
            }
            else if (HTS_TYPE::VCF == currentHtsType)
            {
                const vcf_record& vcfRecord(streamData.getCurrentVcf());
                if (INPUT_TYPE::CANDIDATE_INDELS == currentIndex)     // process candidate indels input from vcf file(s)
                {
                    if (vcfRecord.is_indel())
                    {
                        process_candidate_indel(opt.max_indel_size, vcfRecord, sppr);
                    }
                    else
                    {
                        log_os << "WARNING: candidate indel vcf variant record cannot be categorized as indel:\n";
                        streamData.getCurrentVcfStreamer().report_state(log_os);
                    }
                }
                else if (INPUT_TYPE::FORCED_GT_VARIANTS ==
                         currentIndex)     // process forced genotype tests from vcf file(s)
                {
                    if (vcfRecord.is_indel())
                    {
                        static const unsigned sample_no(0);
                        static const bool is_forced_output(true);
                        process_candidate_indel(opt.max_indel_size, vcfRecord, sppr, sample_no, is_forced_output);
                    }
                    else if (vcfRecord.is_snv() or vcfRecord.is_ref_site())
                    {
                        sppr.insert_forced_output_pos(vcfRecord.pos - 1);
                    }
                    else
                    {
                        std::ostringstream oss;
                        oss << "ERROR: forcedGT vcf variant record cannot be categorized as SNV or indel:\n";
                        streamData.getCurrentVcfStreamer().report_state(oss);
                        BOOST_THROW_EXCEPTION(LogicException(oss.str()));
                    }
                }
                else if (INPUT_TYPE::NOISE_VARIANTS == currentIndex)
                {
                    if (vcfRecord.is_snv())
                    {
                        SiteNoise sn;
                        set_noise_from_vcf(vcfRecord.line, sn);
                        sppr.insert_noise_pos(vcfRecord.pos - 1, sn);
                    }
                }
                else
                {
                    assert(false && "Unexpected hts index");
                }
            }
            else
            {
                assert(false && "Invalid input condition");
            }
        }
    }
    sppr.reset();
}
