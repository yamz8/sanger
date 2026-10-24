---
name: sanger
description: |
  Inspect, trim, align, and build consensus sequences from Sanger sequencing
  traces with the sanger command. Use for ANY request about .ab1, .abi, or
  .scf trace files: checking read quality, trimming low-quality ends,
  verifying a clone or plasmid against a reference, finding mismatches, or
  writing a consensus FASTA.
---

# Sanger

`sanger` reads AB1 (ABIF) and SCF chromatogram files. Every command below
works without a display; `sanger open <files>` opens the viewer for a person.

Positions are one-based. Reference positions count along the reference;
read positions count along the read as it was sequenced, so a reverse read's
position 1 is its first base off the sequencer, not the reference's left end.

## Workflow

1. Look at the reads: `sanger info reads/*.ab1 --json` gives each trace's
   length, mean quality, and the stretch Mott trimming keeps
   (`trimStart`-`trimEnd`). A read with a short trimmed length or a mean
   quality under 20 is a failed read; say so rather than trusting it.
2. Align to the reference: `sanger align reference.fasta reads/*.ab1 --json`.
   Each read gets a strand (reverse reads are detected and flipped), its
   reference span, and identity. `"aligned": false` means it doesn't come
   from that reference. `mismatches` lists every reference position where a
   read disagrees, with each read's call and quality. An insertion relative
   to the reference has `"insert": k` for the k-th base after `position`;
   a deletion shows the read's call as `-`.
3. Judge each mismatch: one supported by several reads at quality 30 or more
   is real; one in a single read at low quality (under 20) while others agree
   with the reference is a sequencing error. Only a person looking at the
   chromatogram can settle a doubtful one, so name those positions.
4. Write the consensus: `sanger consensus reference.fasta reads/*.ab1 -o
   consensus.fasta`. Each read votes with its call's quality; ties become N,
   and reference stretches no read covers become N.

`--error-limit` (default 0.05) sets the trim: lower trims harder. `sanger trim
reads/*.ab1 -o trimmed.fasta` writes trimmed reads without a reference.

Every command exits 1 with the reason on stderr when a file can't be read.
