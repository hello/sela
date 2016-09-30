//SimplE Lossless Audio Encoder
//Released under MIT License

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "lpc.h"
#include "rice.h"
#include "wavutils.h"
#include "apev2.h"

#define SHORT_MAX 32767
#define BLOCK_SIZE 240

int main(int argc,char **argv)
{
	if(argc < 3)
	{
		fprintf(stderr,"Usage : %s <input.wav> <output.hlo>\n",argv[0]);
		fprintf(stderr,"Supports Redbook CD quality (16 bits/sample) WAVEform audio(*.wav) files only.\n");
		return -1;
	}

	FILE *infile = fopen(argv[1],"rb");
	FILE *outfile = fopen(argv[2],"wb");

	if(infile == NULL || outfile == NULL)
	{
		fprintf(stderr,"Error open input or output file. Exiting.......\n");
		if(infile != NULL)
			fclose(infile);
		if(outfile != NULL)
			fclose(outfile);
		return -1;
	}
	else
	{

		fprintf(stderr,"Input : %s\n",argv[1]);
		fprintf(stderr,"Output : %s\n",argv[2]);
	}

	//Variables
	int8_t percent = 0;
	uint8_t opt_lpc_order = 0;
	int16_t channels,bps;
	uint8_t rice_param_ref,rice_param_residue;
	const char magic_number[5] = "hello";
	uint16_t req_int_ref,req_int_residues,samples_per_channel;
	const int16_t Q = 35;
	int32_t i,j,k = 0;
	int32_t sample_rate,read_size;
	const uint32_t metadata_sync = 0xAA5500FF;
	uint32_t metadata_size = 0;
	const uint32_t frame_sync = 0xAA55FF00;
	int32_t frame_sync_count = 0;
	uint32_t req_bits_ref,req_bits_residues;
	uint32_t seconds,samples;
	const int64_t corr = ((int64_t)1) << Q;
	uint64_t in_file_size,out_file_size;

	//Arrays
	int16_t short_samples[BLOCK_SIZE];
	int32_t qtz_ref_coeffs[MAX_LPC_ORDER];
	int32_t int_samples[BLOCK_SIZE];
	int32_t residues[BLOCK_SIZE];
	int32_t spar[MAX_LPC_ORDER];
	uint32_t unsigned_ref[MAX_LPC_ORDER];
	uint32_t encoded_ref[MAX_LPC_ORDER];
	uint32_t u_residues[BLOCK_SIZE];
	uint32_t encoded_residues[BLOCK_SIZE];
	int64_t lpc[MAX_LPC_ORDER + 1];
	size_t written;
	int32_t qtz_samples[BLOCK_SIZE];
	int32_t autocorr[MAX_LPC_ORDER + 1];
	int32_t ref[MAX_LPC_ORDER];
	int32_t lpc_mat[MAX_LPC_ORDER][MAX_LPC_ORDER];
	
	//Metadata data structures
	wav_tags tags;
	apev2_state state;
	apev2_keys keys_inst;
	apev2_hdr_ftr header;
	apev2_item_list ape_list;

	//Init wav tags
	init_wav_tags(&tags);

	//Init apev2 state
	init_apev2(&state);

	//Init apev2 keys
	init_apev2_keys(&state,&keys_inst);

	//Init apev2 header
	init_apev2_header(&state,&header);

	//Init apev2 item link list
	init_apev2_item_list(&state,&ape_list);

	//Check the wav file
	int32_t is_wav = check_wav_file(infile,&sample_rate,&channels,&bps,&samples,&tags);
	uint32_t estimated_frames = ceil((float)samples/(channels * BLOCK_SIZE * sizeof(int16_t)));

	switch(is_wav)
	{
		case READ_STATUS_OK:
			fprintf(stderr,"WAV file detected\n");
			break;
		case READ_STATUS_OK_WITH_META:
			fprintf(stderr,"WAV file detected\n");
			break;
		case ERR_NO_RIFF_MARKER:
			fprintf(stderr,"RIFF header not found. Exiting......\n");
			fclose(infile);
			fclose(outfile);
			return -1;
		case ERR_NO_WAVE_MARKER:
			fprintf(stderr,"WAVE header not found. Exiting......\n");
			fclose(infile);
			fclose(outfile);
			return -1;
		case ERR_NO_FMT_MARKER:
			fprintf(stderr,"No Format chunk found. Exiting......\n");
			fclose(infile);
			fclose(outfile);
			return -1;
		case ERR_NOT_A_PCM_FILE:
			fprintf(stderr,"Not a PCM file. Exiting.....\n");
			fclose(infile);
			fclose(outfile);
			return -1;
		default:
			fprintf(stderr,"Some error occured. Exiting.......\n");
			return -1;
	}

	if(bps != 16)
	{
		fprintf(stderr,"Supports only 16 bits/sample WAVEform audio files. Exiting.......\n");
		return -1;
	}

	//Print media info
	fprintf(stderr,"\nStream Information\n");
	fprintf(stderr,"------------------\n");
	fprintf(stderr,"Sampling Rate : %d Hz\n",sample_rate);
	fprintf(stderr,"Bits per sample : %d\n",bps);
	fprintf(stderr,"Channels : %d ",channels);
	if(channels == 1)
		fprintf(stderr,"(Monoaural)\n");
	else if(channels == 2)
		fprintf(stderr,"(Stereo)\n");
	else
		fprintf(stderr,"\n");

	//Print metadata
	fprintf(stderr,"\nMetadata\n");
	fprintf(stderr,"--------\n");

	if(is_wav == READ_STATUS_OK_WITH_META)
	{
		if(tags.title_present)
			fprintf(stderr,"Title : %s\n",tags.title);
		if(tags.artist_present)
			fprintf(stderr,"Artist : %s\n",tags.artist);
		if(tags.album_present)
			fprintf(stderr,"Album : %s\n",tags.album);
		if(tags.genre_present)
			fprintf(stderr,"Genre : %s\n",tags.genre);
		if(tags.year_present)
			fprintf(stderr,"Year : %d\n",atoi(tags.year));

		//Wav to apev2 tags
		wav_to_apev2(&state,&tags,&ape_list,&keys_inst);
		//wav_to_apev2(&state,&tags,&ape_list,&keys_inst);
		finalize_apev2_header(&state,&header,&ape_list);
	}
	else
		fprintf(stderr,"No metadata found.\n");


	//Write magic number to output
	written = fwrite(magic_number,sizeof(char),sizeof(magic_number),outfile);

	//Write Media info to output
	written = fwrite(&sample_rate,sizeof(int32_t),1,outfile);
	written = fwrite(&bps,sizeof(int16_t),1,outfile);
	written = fwrite((int8_t *)&channels,sizeof(int8_t),1,outfile);
    written = fwrite(&estimated_frames,sizeof(int32_t),1,outfile);

	//Define read size
	read_size = channels * BLOCK_SIZE;
	int16_t *buffer = (int16_t *)malloc(sizeof(int16_t) * read_size);

	//Main loop
	while(feof(infile) == 0)
	{
		//Read Samples from input
		size_t read = fread(buffer,sizeof(int16_t),read_size,infile);

		samples_per_channel = read/channels;

		//Write frame syncword
		written = fwrite(&frame_sync,sizeof(int32_t),1,outfile);

		frame_sync_count++;

		for(i = 0; i < channels; i++)
		{
        
			//Separate channels
			for(j = 0; j < samples_per_channel; j++)
				short_samples[j] = buffer[channels * j + i];
			//Quantize sample data
			for(j = 0; j < samples_per_channel; j++)
				qtz_samples[j] = (short_samples[j]<<15);
			//Calculate autocorrelation data
			auto_corr_fun(qtz_samples,samples_per_channel,MAX_LPC_ORDER,1,
				autocorr);
			//Calculate reflection coefficients
			opt_lpc_order = compute_ref_coefs(autocorr,MAX_LPC_ORDER,ref);
			//Quantize reflection coefficients
			qtz_ref_cof(ref,opt_lpc_order,qtz_ref_coeffs);
			//signed to unsigned
			signed_to_unsigned(opt_lpc_order,qtz_ref_coeffs,unsigned_ref);

			//get optimum rice param and number of bits
			rice_param_ref = get_opt_rice_param(unsigned_ref,opt_lpc_order,
				&req_bits_ref);
			//Encode ref coeffs
			req_bits_ref = rice_encode_block(rice_param_ref,unsigned_ref,
				opt_lpc_order,encoded_ref);
			//Determine number of ints required for storage
			req_int_ref = (req_bits_ref+31)/(32);

			//Dequantize reflection
			dqtz_ref_cof(qtz_ref_coeffs,opt_lpc_order,ref);

			//Reflection to lpc
			levinson(NULL,opt_lpc_order,ref,lpc_mat);
            
			lpc[0] = 0;
			for(j = 0; j < opt_lpc_order; j++)
				lpc[j + 1] = corr * lpc_mat[opt_lpc_order - 1][j];

			for(j = opt_lpc_order; j < MAX_LPC_ORDER; j++)
				lpc[j + 1] = 0;

			//Copy samples
			for(j = 0; j < samples_per_channel; j++)
				int_samples[j] = short_samples[j];

			//Get residues
			calc_residue(int_samples,samples_per_channel,opt_lpc_order,Q,lpc,
				residues);

			//signed to unsigned
			signed_to_unsigned(samples_per_channel,residues,u_residues);

			//get optimum rice param and number of bits
			rice_param_residue = get_opt_rice_param(u_residues,
				samples_per_channel,&req_bits_residues);

			//Encode residues
			req_bits_residues = rice_encode_block(rice_param_residue,u_residues,
				samples_per_channel,encoded_residues);

			//Determine nunber of ints required for storage
			req_int_residues = ceil((float)(req_bits_residues)/(32));

			//Write channel number to output
			//written = fwrite((char *)&i,sizeof(char),1,outfile);

			//Write rice_params,bytes,encoded lpc_coeffs to output
			written = fwrite(&rice_param_ref,sizeof(uint8_t),1,outfile);
			written = fwrite(&req_int_ref,sizeof(uint16_t),1,outfile);
			written = fwrite(&opt_lpc_order,sizeof(uint8_t),1,outfile);
			written = fwrite(encoded_ref,sizeof(uint32_t),req_int_ref,outfile);

			//Write rice_params,bytes,encoded residues to output
			written = fwrite(&rice_param_residue,sizeof(uint8_t),1,outfile);
			written = fwrite(&req_int_residues,sizeof(uint16_t),1,outfile);
			//written = fwrite(&samples_per_channel,sizeof(uint16_t),1,outfile);
			written = fwrite(encoded_residues,sizeof(uint32_t),req_int_residues,
				outfile);
            
		}

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

	fprintf(stderr,"\n");
	fprintf(stderr,"\nStatistics\n");
	fprintf(stderr,"----------\n");
	seconds = ((uint32_t)(frame_sync_count * BLOCK_SIZE)/(sample_rate));
	fprintf(stderr,"%d frames written (%dmin %dsec)\n",
		frame_sync_count,(seconds/60),(seconds%60));
	fseek(infile,0,SEEK_END);
	fseek(outfile,0,SEEK_END);

	in_file_size = ftell(infile);
	out_file_size = ftell(outfile);
	fprintf(stderr,"Compression Ratio : %0.2f%\n",100 * (float)((float)out_file_size/(float)in_file_size));
	fprintf(stderr,"Bitrate : %d kbps\n",(out_file_size * 8)/(seconds * 1000));
	
	//Cleanup
	free(buffer);
	destroy_wav_tags(&tags);
	free_apev2_list(&state,&ape_list);
	fclose(infile);
	fclose(outfile);

	return 0;
}