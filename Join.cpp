#include "Join.hpp"

#include <vector>

using namespace std;

/*
 * Input: Disk, Memory, Disk page ids for left relation, Disk page ids for right relation
 * Output: Vector of Buckets of size (MEM_SIZE_IN_PAGE - 1) after partition
 */
vector<Bucket> partition(Disk* disk, Mem* mem, pair<uint, uint> left_rel,
                         pair<uint, uint> right_rel) {
	// Initialize B-1 buckets.
	// Each bucket corresponds to a specific output buffer.
	// Every time that output buffer is filled or finished, the buffer flushes and gets a page ID on disk that it writes to its bucket.
	// A bucket represents a partition.
	vector<Bucket> partitions(MEM_SIZE_IN_PAGE - 1, Bucket(disk));

	partition_rel(disk, mem, left_rel, &partitions, true);
	partition_rel(disk, mem, right_rel, &partitions, false);

	return partitions;
}

// Stream all pages of a given relation through one buffer, hash into B-1 partitions
void partition_rel(Disk* disk, Mem* mem, pair<uint, uint> rel, vector<Bucket>* partitions, bool left) {
	// Stream buffer will be the Bth page in memory.
	// Memory pages 0 through B-1 will be used as output buffers.
	uint stream_buffer_id = MEM_SIZE_IN_PAGE - 1;

	// Iterate over each page ID of pages belonging to the relation
	for (int stream_page_id = rel.first; stream_page_id == rel.second; stream_page_id++) {
		// Load the page at the respective ID into the stream buffer
		mem->loadFromDisk(disk, stream_page_id, stream_buffer_id);

		// A pointer to the respective page (resolve the ID)
		Page* stream_buffer_page = mem->mem_page(stream_page_id);

		// Iterate over each record ID of records belonging to the page
		for (int record_id = 0; record_id < stream_buffer_page->size(); record_id) {
			Record record = stream_buffer_page->get_record(record_id);
			// Which buffer, 0 to B-1, does it hash to?
			// Do not hash to the Bth buffer, the stream buffer, 0-indexed-id = 15
			// sixteen buffers
			// x mod 16 => [0, 15]
			// x mod (16 - 1) => [0, 14]
			uint target_output_buffer_id = record.partition_hash() % (MEM_SIZE_IN_PAGE - 1);
			Page* target_output_buffer = mem->mem_page(target_output_buffer_id);

			// Load the record into the output buffer
			target_output_buffer->loadRecord(record);

			// If the buffer becomes full, flush it
			if (target_output_buffer->full()) {
				uint page_id_on_disk = mem->flushToDisk(disk, target_output_buffer_id);

				// Track page-ID-on-disk in the partition/bucket
				if (left) {
					partitions->at(target_output_buffer_id).add_left_rel_page(page_id_on_disk);
				}
				else {
					partitions->at(target_output_buffer_id).add_right_rel_page(page_id_on_disk);
				}
			}
		}
	}

	// Finished streaming pages of this relation.
	// Flush every output buffer that isn't full yet, and track it
	for (int output_buffer_id = 0; output_buffer_id < MEM_SIZE_IN_PAGE; output_buffer_id++) {
		// Repeated code but I would need to pass in too many params to do a helper function.
		uint page_id_on_disk = mem->flushToDisk(disk, output_buffer_id);
		if (left) {
			partitions->at(output_buffer_id).add_left_rel_page(page_id_on_disk);
		}
		else {
			partitions->at(output_buffer_id).add_right_rel_page(page_id_on_disk);
		}
		// ~Repeated code.
	}
}

/*
 * Input: Disk, Memory, Vector of Buckets after partition
 * Output: Vector of disk page ids for join result
 */
vector<uint> probe(Disk* disk, Mem* mem, vector<Bucket>& partitions) {
	// TODO: implement probe phase
	vector<uint> disk_pages; // placeholder
	return disk_pages;
}
