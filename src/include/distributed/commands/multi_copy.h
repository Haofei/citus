/*-------------------------------------------------------------------------
 *
 * multi_copy.h
 *    Declarations for public functions and variables used in COPY for
 *    distributed tables.
 *
 * Copyright (c) Citus Data, Inc.
 *
 *-------------------------------------------------------------------------
 */

#ifndef MULTI_COPY_H
#define MULTI_COPY_H


#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"
#include "parser/parse_coerce.h"
#include "tcop/dest.h"

#include "distributed/metadata_cache.h"
#include "distributed/metadata_utility.h"
#include "distributed/version_compat.h"


#define INVALID_PARTITION_COLUMN_INDEX -1


/*
 * CitusCopyDest indicates the source or destination of a COPY command.
 */
typedef enum CitusCopyDest
{
	COPY_FILE,                  /* to/from file (or a piped program) */
	COPY_FRONTEND,              /* to frontend */
	COPY_CALLBACK               /* to/from callback function */
} CitusCopyDest;


/*
 * A smaller version of copy.c's CopyStateData, trimmed to the elements
 * necessary to copy out results. While it'd be a bit nicer to share code,
 * it'd require changing core postgres code.
 */
typedef struct CopyOutStateData
{
	CitusCopyDest copy_dest;    /* type of copy source/destination */
	StringInfo fe_msgbuf;       /* used for all dests during COPY TO, only for
	                             * dest == COPY_NEW_FE in COPY FROM */
	List *attnumlist;           /* integer list of attnums to copy */
	int file_encoding;          /* file or remote side's character encoding */
	bool need_transcoding;              /* file encoding diff from server? */
	bool binary;                /* binary format? */
	char *null_print;           /* NULL marker string (server encoding!) */
	char *null_print_client;            /* same converted to file encoding */
	char *delim;                /* column delimiter (must be 1 byte) */

	MemoryContext rowcontext;   /* per-row evaluation context */
} CopyOutStateData;

typedef struct CopyOutStateData *CopyOutState;

/* struct type to keep both hostname and port */
typedef struct NodeAddress
{
	char *nodeName;
	int32 nodePort;
} NodeAddress;

/* struct to allow rReceive to coerce tuples before sending them to workers  */
typedef struct CopyCoercionData
{
	CoercionPathType coercionType;
	FmgrInfo coerceFunction;

	FmgrInfo inputFunction;
	FmgrInfo outputFunction;
	Oid typioparam; /* inputFunction has an extra param */
} CopyCoercionData;

/* CopyDestReceiver can be used to stream results into a distributed table */
typedef struct CitusCopyDestReceiver
{
	/* public DestReceiver interface */
	DestReceiver pub;

	/* relation and columns to which to copy */
	Oid distributedRelationId;
	List *columnNameList;
	int partitionColumnIndex;

	/* open relation handle */
	Relation distributedRelation;

	/* descriptor of the tuples that are sent to the worker */
	TupleDesc tupleDescriptor;

	/* EState for per-tuple memory allocation */
	EState *executorState;

	/* MemoryContext for DestReceiver session */
	MemoryContext memoryContext;

	/* template for COPY statement to send to workers */
	CopyStmt *copyStatement;

	/*
	 * shardId to CopyShardState map. Also used in insert_select_executor.c for
	 * task pruning.
	 */
	HTAB *shardStateHash;

	/* socket to CopyConnectionState map */
	HTAB *connectionStateHash;

	/* state on how to copy out data types */
	CopyOutState copyOutState;
	FmgrInfo *columnOutputFunctions;

	/* instructions for coercing incoming tuples */
	CopyCoercionData *columnCoercionPaths;

	/* number of tuples sent */
	int64 tuplesSent;

	/* useful for tracking multi shard accesses */
	bool multiShardCopy;

	/* if true, should copy to local placements in the current session */
	bool shouldUseLocalCopy;

	/*
	 * if true, the data from this dest receiver should be published for CDC clients.
	 * This is set tot false for internal transfers like shard split/move/rebalance etc.
	 */
	bool isPublishable;

	/*
	 * Copy into colocated intermediate result. When this is set, the
	 * COPY assumes there are hypothetical colocated shards to the
	 * relation that are files. And, COPY writes the data to the
	 * files as if they are shards.
	 */
	char *colocatedIntermediateResultIdPrefix;

	/*
	 * When copying into append-partitioned tables, the destination shard is chosen
	 * upfront.
	 */
	uint64 appendShardId;

	/*
	 * When copying to intermediate files, we can skip coercions and run them
	 * when merging into the target tables.
	 */
	bool skipCoercions;

	/*
	 * Determines whether the COPY command should track query stat counters.
	 */
	bool trackQueryCounters;
} CitusCopyDestReceiver;


/* GUCs */
extern bool SkipJsonbValidationInCopy;

/* managed via GUC, the default is 4MB */
extern int CopySwitchOverThresholdBytes;


/* function declarations for copying into a distributed table */
extern CitusCopyDestReceiver * CreateCitusCopyDestReceiver(Oid relationId,
														   List *columnNameList,
														   int partitionColumnIndex,
														   EState *executorState,
														   char *intermediateResultPrefix,
														   bool isPublishable,
														   bool trackQueryCounters);
extern FmgrInfo * ColumnOutputFunctions(TupleDesc rowDescriptor, bool binaryFormat);
extern bool CanUseBinaryCopyFormat(TupleDesc tupleDescription);
extern bool CanUseBinaryCopyFormatForTargetList(List *targetEntryList);
extern bool CanUseBinaryCopyFormatForType(Oid typeId);
extern void AppendCopyRowData(Datum *valueArray, bool *isNullArray,
							  TupleDesc rowDescriptor,
							  CopyOutState rowOutputState,
							  FmgrInfo *columnOutputFunctions,
							  CopyCoercionData *columnCoercionPaths);
extern void AppendCopyBinaryHeaders(CopyOutState headerOutputState);
extern void AppendCopyBinaryFooters(CopyOutState footerOutputState);
extern void EndRemoteCopy(int64 shardId, List *connectionList);
extern List * CreateRangeTable(Relation rel);
extern Node * ProcessCopyStmt(CopyStmt *copyStatement,
							  QueryCompletion *completionTag,
							  const char *queryString);
extern void CheckCopyPermissions(CopyStmt *copyStatement);
extern bool IsCopyResultStmt(CopyStmt *copyStatement);
extern void ConversionPathForTypes(Oid inputType, Oid destType, CopyCoercionData *result);
extern Datum CoerceColumnValue(Datum inputValue, CopyCoercionData *coercionPath);
extern void ReportCopyError(MultiConnection *connection, PGresult *result);


#endif /* MULTI_COPY_H */
