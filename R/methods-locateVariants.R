## add methods to allow VariantAnnotation::locateVariants() to
## annotate separately with 5' and 3' splice sites

## this is adapted from method VariantAnnotation::SpliceSiteVariants
setMethod("locateVariants", c("GRanges", "GRangesList", "FiveSpliceSiteVariants"),
          function(query, subject, region, ..., ignore.strand=FALSE, asHits=FALSE) {
            .fiveSpliceSites(query, subject, region, ignore.strand=ignore.strand, asHits=asHits)
          })

setMethod("locateVariants", c("GRanges", "GRangesList", "ThreeSpliceSiteVariants"),
          function(query, subject, region, ..., ignore.strand=FALSE, asHits=FALSE) {
            .threeSpliceSites(query, subject, region, ignore.strand=ignore.strand, asHits=asHits)
          })

## adapted from function VariantAnnotation:::.spliceSite
.fiveSpliceSites <- function(query, subject, region, ignore.strand, asHits, ...) {
  ## Overlap any portion of first upstream and last dowstream nucleotides of the 5' end of introns
  usub <- unlist(subject, use.names=FALSE)

  int_start <- GRanges(seqnames(usub),
                       IRanges(ifelse(strand(usub) == "+", start(usub)-upstream(region),
                                      end(usub)-downstream(region)-1),
                               ifelse(strand(usub) == "+", start(usub)+downstream(region)+1,
                                      end(usub)+upstream(region))),
                       strand=strand(usub))
  fo <- findOverlaps(query, int_start, type="any", ignore.strand=ignore.strand)

  if (asHits)
    return(VariantAnnotation:::.consolidateHits(fo, length(query), length(subject),
                                                elementLengths(subject)))
  if (length(fo) > 0) {
    df <- unique(data.frame(queryid=queryHits(fo),
                            usubjectid=subjectHits(fo),
                            subjectid=togroup(subject)[subjectHits(fo)]))

    ## restrict splice site annotations to those occurring in introns of a minimum length
    df <- df[unlist(width(subject), use.names=FALSE)[df$subjectid] > minIntronLength(region), ]

    GRanges(seqnames=seqnames(query)[df$queryid],
            ranges=IRanges(ranges(query)[df$queryid]),
            strand=strand(int_start)[df$usubjectid],
            LOCATION=rep("fiveSpliceSite", length(df$queryid)),
            LOCSTART=start(int_start)[df$usubjectid],
            LOCEND=end(int_start)[df$usubjectid],
            QUERYID=df$queryid,
            TXID=as.integer(names(subject)[df$subjectid]),
            CDSID=IntegerList(integer(0)),
            GENEID=NA_character_,
            PRECEDEID=CharacterList(character(0)),
            FOLLOWID=CharacterList(character(0)))
  } else {
    res <- GRanges()
    values(res) <- DataFrame(LOCATION=character(0),
                             LOCSTART=integer(), LOCEND=integer(),
                             QUERYID=integer(), TXID=integer(),
                             CDSID=IntegerList(), GENEID=character(),
                             PRECEDEID=CharacterList(),
                             FOLLOWID=CharacterList())
    res
  }
}

## adapted from function VariantAnnotation:::.spliceSite
.threeSpliceSites <- function(query, subject, region, ignore.strand, asHits, ...) {
  ## Overlap any portion of first upstream and last dowstream nucleotides of the 5' end of introns
  usub <- unlist(subject, use.names=FALSE)

  int_end <- GRanges(seqnames(usub),
                     IRanges(ifelse(strand(usub) == "+", end(usub)-upstream(region)-1,
                                    start(usub)-downstream(region)),
                             ifelse(strand(usub) == "+", end(usub)+downstream(region),
                                    start(usub)+upstream(region)+1)),
                     strand=strand(usub))
  fo <- findOverlaps(query, int_end, type="any", ignore.strand=ignore.strand)

  if (asHits)
    return(VariantAnnotation:::.consolidateHits(fo, length(query), length(subject),
                                                elementLengths(subject)))
  if (length(fo) > 0) {
    df <- unique(data.frame(queryid=queryHits(fo),
                            usubjectid=subjectHits(fo),
                            subjectid=togroup(subject)[subjectHits(fo)]))

    ## restrict splice site annotations to those occurring in introns of a minimum length
    df <- df[unlist(width(subject), use.names=FALSE)[df$subjectid] > minIntronLength(region), ]

    GRanges(seqnames=seqnames(query)[df$queryid],
            ranges=IRanges(ranges(query)[df$queryid]),
            strand=strand(int_end)[df$usubjectid],
            LOCATION=rep("threeSpliceSite", length(df$queryid)),
            LOCSTART=start(int_end)[df$usubjectid],
            LOCEND=end(int_end)[df$usubjectid],
            QUERYID=df$queryid,
            TXID=as.integer(names(subject)[df$subjectid]),
            CDSID=IntegerList(integer(0)),
            GENEID=NA_character_,
            PRECEDEID=CharacterList(character(0)),
            FOLLOWID=CharacterList(character(0)))
  } else {
    res <- GRanges()
    values(res) <- DataFrame(LOCATION=character(0),
                             LOCSTART=integer(), LOCEND=integer(),
                             QUERYID=integer(), TXID=integer(),
                             CDSID=IntegerList(), GENEID=character(),
                             PRECEDEID=CharacterList(),
                             FOLLOWID=CharacterList())
    res
  }
}