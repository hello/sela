#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rice.h"
#include "lpc.h"
#include "wavutils.h"
#include "apev2.h"
#define BLOCK_SIZE 240

int main(int argc,char **argv)
{
	
	if(argc < 3)
	{
		fprintf(stderr,"Usage\n%s input.hlo output.wav\n",argv[0]);
		return -1;
	}

	FILE *infile = fopen(argv[1],"rb");
	FILE *outfile = fopen(argv[2],"wb");
	fprintf(stderr,"Input : %s\n",argv[1]);
	fprintf(stderr, "Output : %s\n",argv[2]);

	char magic_number[4];
	size_t read_bytes = fread(magic_number,sizeof(char),5,infile);
	if(strncmp(magic_number,"hello",5))
	{
		fprintf(stderr,"Not a hello file.\n");
		fclose(infile);
		fclose(outfile);
		return -1;
	}

	//Variables
	int8_t percent = 0;
	uint8_t channels,curr_channel,rice_param_ref,rice_param_residue;
	int16_t bps,meta_present = 0;
	const int16_t Q = 35;
	uint16_t num_ref_elements,num_residue_elements,samples_per_channel = 0;
	int32_t sample_rate,i;
	int32_t frame_sync_count = 0;
	uint32_t temp;
	uint32_t seconds,estimated_frames;
	const uint32_t frame_sync = 0xAA55FF00;
	const uint32_t metadata_sync = 0xAA5500FF;
	const int64_t corr = ((int64_t)1) << Q;
	size_t read,written;

	//Arrays
	int32_t s_residues[BLOCK_SIZE];
	int32_t rcv_samples[BLOCK_SIZE];
	uint32_t compressed_residues[BLOCK_SIZE];
	uint32_t decomp_residues[BLOCK_SIZE];
	
	//Metadata structures
	apev2_state read_state;
	apev2_keys keys_inst;
	apev2_item_list ape_read_list;
	apev2_hdr_ftr read_header;

	//Initialise state
	init_apev2(&read_state);

	//Initialise apev2 keys
	init_apev2_keys(&read_state,&keys_inst);

	//Init apev2 header
	init_apev2_header(&read_state,&read_header);

	//Init apev2 list
	init_apev2_item_list(&read_state,&ape_read_list);

	//Read media info from input file
	read = fread(&sample_rate,sizeof(int32_t),1,infile);
	read = fread(&bps,sizeof(int16_t),1,infile);
	read = fread(&channels,sizeof(int8_t),1,infile);
	read = fread(&estimated_frames,sizeof(uint32_t),1,infile);

	fprintf(stderr,"\nStream Information\n");
	fprintf(stderr,"------------------\n");
	fprintf(stderr,"Sample rate : %d Hz\n",sample_rate);
	fprintf(stderr,"Bits per sample : %d\n",bps);
	fprintf(stderr,"Channels : %d",channels);

	if(channels == 1)
		fprintf(stderr,"(Monoaural)\n");
	else if(channels == 2)
		fprintf(stderr,"(Stereo)\n");
	else
		fprintf(stderr,"\n");

	fprintf(stderr,"\nMetadata\n");
	fprintf(stderr,"--------\n");
	if(meta_present == 0)
		fprintf(stderr,"No metadata found\n");
	else
		print_apev2_tags(&read_state,&ape_read_list);

	wav_header hdr;
	initialize_header(&hdr,channels,sample_rate,bps);
	write_header(outfile,&hdr);

	int16_t *buffer = (int16_t *)calloc((BLOCK_SIZE * channels),sizeof(int16_t));

	//Main loop
	while(feof(infile) == 0)
	{
		read = fread(&temp,sizeof(int32_t),1,infile);//Read from input
		if(temp == frame_sync)
		{
			frame_sync_count++;
			for(i = 0; i < channels; i++)
			{
				//Read channel number
				//read = fread(&curr_channel,sizeof(uint8_t),1,infile);
                curr_channel = 0;

				//Read rice_param,num_of_residues,encoded residues from input
				read = fread(&rice_param_residue,sizeof(uint8_t),1,infile);
				read = fread(&num_residue_elements,sizeof(uint16_t),1,infile);
                
                samples_per_channel = BLOCK_SIZE;
                
				read = fread(compressed_residues,sizeof(uint32_t),num_residue_elements,infile);

				//Decode compressed reflection coefficients and residues
				rice_decode_block(rice_param_residue,compressed_residues,samples_per_channel,decomp_residues);

				//unsigned to signed
				unsigned_to_signed(samples_per_channel,decomp_residues,s_residues);

				//Combine samples from all channels
				for(int32_t k = 0; k < samples_per_channel; k++)
					buffer[channels * k + i] = (int16_t)s_residues[k];
			}
			written = fwrite(buffer,sizeof(int16_t),(samples_per_channel * channels),outfile);
			temp = 0;

			//Percentage bar print
			fprintf(stderr,"\r");
			percent = ((float)frame_sync_count/(float)estimated_frames) * 100;
			fprintf(stderr,"[");
			for(i = 0; i < (percent >> 2); i++)
				fprintf(stderr,"=");
			for(i = 0; i < (25 - (percent >> 2)); i++)
				fprintf(stderr," ");
			fprintf(stderr,"]");
		}
		else
			break;
	}

	fprintf(stderr,"\n");
	seconds = ((uint32_t)(frame_sync_count * BLOCK_SIZE)/(sample_rate));
	fprintf(stderr,"\nStatistics\n");
	fprintf(stderr,"----------\n");
	fprintf(stderr,"%d frames decoded. %dmin %dsec of audio\n",
		frame_sync_count,(seconds/60),(seconds%60));
	finalize_file(outfile);

	//Cleanup
	free(buffer);
	fclose(infile);
	fclose(outfile);
	free_apev2_list(&read_state,&ape_read_list);

	return 0;
}
