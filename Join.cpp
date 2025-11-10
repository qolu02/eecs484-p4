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
	Page* stream_buffer_page = mem->mem_page(stream_buffer_id);

	// Iterate over each page ID of pages belonging to the relation
	for (int stream_page_id = rel.first; stream_page_id < rel.second; stream_page_id++) {
		// Load the page at the respective ID into the stream buffer
		mem->loadFromDisk(disk, stream_page_id, stream_buffer_id);
		// The streamed page will now only be accessed from the stream buffer

		// Iterate over each record ID of records belonging to the page
		for (int record_id = 0; record_id < stream_buffer_page->size(); record_id++) {
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
	// Do not flush the Bth buffer (stream buffer)
	for (int output_buffer_id = 0; output_buffer_id < MEM_SIZE_IN_PAGE - 1; output_buffer_id++) {
		if (mem->mem_page(output_buffer_id)->empty()) {
			continue;
		}
		// Repeated code but I would need to pass in too many params to do a helper function.
		// I don't feel like figuring out how to store all the params in a class and write member functions.
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
	vector<uint> disk_pages;

	// Which relation is smaller?
	uint num_left_records = 0;
	uint num_right_records = 0;
	for (Bucket e : partitions) {
		num_left_records += e.num_left_rel_record;
		num_right_records += e.num_right_rel_record;
	}
	bool left_smaller = num_left_records < num_right_records;

	// For each partition of the smaller relation, stream the pages of the larger relation
	for (uint partition_id = 0; partition_id < partitions.size(); partition_id++) {
		// The respective partition
		Bucket partition = partitions[partition_id];

		vector<uint> srel;
		vector<uint> lrel;
		
		if (left_smaller) {
			srel = partition.get_left_rel(); // srel: smaller relation	
			lrel = partition.get_right_rel(); // lrel: larger relation
		}
		else {
			srel = partition.get_right_rel(); // srel: smaller relation
			lrel = partition.get_left_rel(); // lrel: larger relation
		}

		uint srel_size = srel.size();
		uint lrel_size = lrel.size();

		// 0 to B-2th buffers are srel buffers.
		// B-1 is the stream input buffer.
		// Bth is the output buffer.

		// stream buffer B-1 (0-indexed: B-2)
		uint stream_buffer_id = MEM_SIZE_IN_PAGE - 2;
		Page* stream_buffer_page = mem->mem_page(stream_buffer_id);

		// output buffer B (0-indexed: B-1)
		uint output_buffer_id = MEM_SIZE_IN_PAGE - 1;
		Page* output_buffer_page = mem->mem_page(output_buffer_id);

		// clear layover from previous partition in hash buffers
		for (uint hash_buffer_id = 0; hash_buffer_id < MEM_SIZE_IN_PAGE - 2; hash_buffer_id++) {
			mem->mem_page(hash_buffer_id)->reset();
		}

		// Over each page of the smaller relation (0 to up to B-2th)
		for (uint srel_page_index = 0; srel_page_index < srel_size; srel_page_index++) {
			// Load the xth srel page into the stream input buffer
			mem->loadFromDisk(disk, srel[srel_page_index], stream_buffer_id);

			// Iterate over each record ID of records belonging to the page
			for (int srel_record_id = 0; srel_record_id < stream_buffer_page->size(); srel_record_id++) {
				Record record = stream_buffer_page->get_record(srel_record_id);

				// x mod (16 - 2)
				// x mod 14 => [0, 13]
				uint target_hash_buffer_id = record.probe_hash() % (MEM_SIZE_IN_PAGE - 2);
				Page* target_hash_buffer = mem->mem_page(target_hash_buffer_id);

				// Load the record into the output buffer
				target_hash_buffer->loadRecord(record);
			}
		}

		// Hash table over smaller relation is set up in hash buffers
		// Now, stream in pages of the larger relation
		for (uint lrel_page_index = 0; lrel_page_index < lrel_size; lrel_page_index++) {
			// Load the xth srel page into the stream input buffer
			mem->loadFromDisk(disk, lrel[lrel_page_index], stream_buffer_id);

			// Iterate over each record ID of records belonging to the page
			for (int lrel_record_id = 0; lrel_record_id < stream_buffer_page->size(); lrel_record_id++) {
				Record lrel_record = stream_buffer_page->get_record(lrel_record_id);

				uint target_hash_buffer_id = lrel_record.probe_hash() % (MEM_SIZE_IN_PAGE - 2);
				Page* target_hash_buffer = mem->mem_page(target_hash_buffer_id);

				// Compare the record to existing records in the partition
				for (int srel_record_id = 0; srel_record_id < target_hash_buffer->size(); srel_record_id++) {
					Record srel_record = target_hash_buffer->get_record(srel_record_id);

					if (lrel_record == srel_record) {
						
						if (left_smaller) {
							output_buffer_page->loadPair(srel_record, lrel_record);
						}
						else {
							output_buffer_page->loadPair(lrel_record, srel_record);
						}

						if (output_buffer_page->full()) {
							uint page_id_on_disk = mem->flushToDisk(disk, output_buffer_id);
							disk_pages.push_back(page_id_on_disk);
						}
					}
				}
			}
		}

		if (!output_buffer_page->empty()) {
			uint page_id_on_disk = mem->flushToDisk(disk, output_buffer_id);
			disk_pages.push_back(page_id_on_disk);
		}
	}

	return disk_pages;
}
