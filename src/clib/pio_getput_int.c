/**
 * @file
 * Internal PIO functions to get and put attributes and data
 * (excluding varm functions).
 *
 * @author Ed Hartnett
 * @date  2016
 *
 * @see http://code.google.com/p/parallelio/
 */

#include <config.h>
#include <pio.h>
#include <pio_internal.h>

/**
 * Write a netCDF attribute of any type, converting to any type.
 *
 * This routine is called collectively by all tasks in the communicator
 * ios.union_comm.
 *
 * @param ncid the ncid of the open file, obtained from
 * PIOc_openfile() or PIOc_createfile().
 * @param varid the variable ID.
 * @param name the name of the attribute.
 * @param atttype the nc_type of the attribute.
 * @param len the length of the attribute array.
 * @param op a pointer with the attribute data.
 * @return PIO_NOERR for success, error code otherwise.
 * @author Ed Hartnett
 */
int PIOc_put_att_tc(int ncid, int varid, const char *name, nc_type atttype,
                    PIO_Offset len, nc_type memtype, const void *op)
{
    iosystem_desc_t *ios;  /* Pointer to io system information. */
    file_desc_t *file;     /* Pointer to file information. */
    PIO_Offset atttype_len;    /* Length (in bytes) of the att type in file. */
    PIO_Offset memtype_len;    /* Length of the att data type in memory. */
    int mpierr = MPI_SUCCESS, mpierr2;  /* Return code from MPI function codes. */
    int ierr = PIO_NOERR;           /* Return code from function calls. */

#ifdef TIMING
    GPTLstart("PIO:PIOc_put_att_tc");
#endif
    /* Find the info about this file. */
    if ((ierr = pio_get_file(ncid, &file)))
        return pio_err(NULL, NULL, ierr, __FILE__, __LINE__);
    ios = file->iosystem;

    /* User must provide some valid parameters. */
    if (!name || !op || strlen(name) > PIO_MAX_NAME || len < 0)
        return pio_err(ios, file, PIO_EINVAL, __FILE__, __LINE__);

    LOG((1, "PIOc_put_att_tc ncid = %d varid = %d name = %s atttype = %d len = %d memtype = %d",
         ncid, varid, name, atttype, len, memtype));

    /* Run these on all tasks if async is not in use, but only on
     * non-IO tasks if async is in use. */
    if (!ios->async || !ios->ioproc)
    {
        /* Get the length (in bytes) of the type in file. */
        ierr = PIOc_inq_type(ncid, atttype, NULL, &atttype_len);
        if(ierr != PIO_NOERR){
            LOG((1, "PIOc_inq_type failed, ierr = %d", ierr));
            return ierr;
        }

        /* Get the length (in bytes) of the type in memory. */
        if (memtype == PIO_LONG_INTERNAL)
            memtype_len = sizeof(long int);
        else
        {
            ierr = PIOc_inq_type(ncid, memtype, NULL, &memtype_len);
            if(ierr != PIO_NOERR){
                LOG((1, "PIOc_inq_type failed, ierr = %d", ierr));
                return ierr;
            }
        }
        LOG((2, "PIOc_put_att atttype_len = %d memtype_len = %d", ncid, atttype_len, memtype_len));
    }

    /* If async is in use, and this is not an IO task, bcast the parameters. */
    if (ios->async)
    {
        int msg = PIO_MSG_PUT_ATT;
        int namelen = strlen(name) + 1;

        PIO_SEND_ASYNC_MSG(ios, msg, &ierr, ncid, varid, namelen, name,
            atttype, len, atttype_len, memtype, memtype_len,
            len * memtype_len, op);
        if(ierr != PIO_NOERR)
        {
            LOG((1, "Error sending async mesg for PIO_MSG_PUT_ATT"));
            return pio_err(ios, NULL, ierr, __FILE__, __LINE__);
        }

        /* Broadcast values currently only known on computation tasks to IO tasks. */
        if ((mpierr = MPI_Bcast(&atttype_len, 1, MPI_OFFSET, ios->comproot, ios->my_comm)))
            check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        if ((mpierr = MPI_Bcast(&memtype_len, 1, MPI_OFFSET, ios->comproot, ios->my_comm)))
            check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        LOG((2, "PIOc_put_att bcast from comproot = %d atttype_len = %d", ios->comproot,
             atttype_len, memtype_len));
    }

    /* If this is an IO task, then call the netCDF function. */
    if (ios->ioproc)
    {
#ifdef _PNETCDF
        if (file->iotype == PIO_IOTYPE_PNETCDF)
        {
            switch(memtype)
            {
            case NC_BYTE:
                ierr = ncmpi_put_att_schar(file->fh, varid, name, atttype, len, op);
                break;
            case NC_CHAR:
                ierr = ncmpi_put_att_text(file->fh, varid, name, len, op);
                break;
            case NC_SHORT:
                ierr = ncmpi_put_att_short(file->fh, varid, name, atttype, len, op);
                break;
            case NC_INT:
                ierr = ncmpi_put_att_int(file->fh, varid, name, atttype, len, op);
                break;
            case PIO_LONG_INTERNAL:
                ierr = ncmpi_put_att_long(file->fh, varid, name, atttype, len, op);
                break;
            case NC_FLOAT:
                ierr = ncmpi_put_att_float(file->fh, varid, name, atttype, len, op);
                break;
            case NC_DOUBLE:
                ierr = ncmpi_put_att_double(file->fh, varid, name, atttype, len, op);
                break;
            default:
                return pio_err(ios, file, PIO_EBADTYPE, __FILE__, __LINE__);
            }
        }
#endif /* _PNETCDF */

#ifdef _ADIOS
        if (file->iotype == PIO_IOTYPE_ADIOS)
        {
            fprintf(stderr,"ADIOS define attribute %s, varid %d, type %d\n", name, varid, atttype);
            enum ADIOS_DATATYPES adios_type = PIOc_get_adios_type(atttype);
            char path[256];
            if (varid != PIO_GLOBAL)
            {
                adios_var_desc_t * av = &(file->adios_vars[varid]);
                strncpy(path, av->name, sizeof(path));
                ++file->adios_vars[varid].nattrs;
            }
            else
            {
                strncpy(path,"pio_global", sizeof(path));
            }
            //fprintf(stderr,"     define %s/%s, type %d\n", path, name, adios_type);
            adios_define_attribute_byvalue(file->adios_group, name, path, adios_type, 1, op);
            ierr = 0;
        }
#endif

        if (file->iotype != PIO_IOTYPE_PNETCDF && file->iotype != PIO_IOTYPE_ADIOS && file->do_io)
        {
            switch(memtype)
            {
#ifdef _NETCDF
            case NC_CHAR:
                ierr = nc_put_att_text(file->fh, varid, name, len, op);
                break;
            case NC_BYTE:
                ierr = nc_put_att_schar(file->fh, varid, name, atttype, len, op);
                break;
            case NC_SHORT:
                ierr = nc_put_att_short(file->fh, varid, name, atttype, len, op);
                break;
            case NC_INT:
                ierr = nc_put_att_int(file->fh, varid, name, atttype, len, op);
                break;
            case PIO_LONG_INTERNAL:
                ierr = nc_put_att_long(file->fh, varid, name, atttype, len, op);
                break;
            case NC_FLOAT:
                ierr = nc_put_att_float(file->fh, varid, name, atttype, len, op);
                break;
            case NC_DOUBLE:
                ierr = nc_put_att_double(file->fh, varid, name, atttype, len, op);
                break;
#endif /* _NETCDF */
#ifdef _NETCDF4
            case NC_UBYTE:
                ierr = nc_put_att_uchar(file->fh, varid, name, atttype, len, op);
                break;
            case NC_USHORT:
                ierr = nc_put_att_ushort(file->fh, varid, name, atttype, len, op);
                break;
            case NC_UINT:
                ierr = nc_put_att_uint(file->fh, varid, name, atttype, len, op);
                break;
            case NC_INT64:
                LOG((3, "about to call nc_put_att_longlong"));
                ierr = nc_put_att_longlong(file->fh, varid, name, atttype, len, op);
                break;
            case NC_UINT64:
                ierr = nc_put_att_ulonglong(file->fh, varid, name, atttype, len, op);
                break;
                /* case NC_STRING: */
                /*      ierr = nc_put_att_string(file->fh, varid, name, atttype, len, op); */
                /*      break; */
#endif /* _NETCDF4 */
            default:
                return pio_err(ios, file, PIO_EBADTYPE, __FILE__, __LINE__);
            }
        }
    }

    ierr = check_netcdf(NULL, file, ierr, __FILE__, __LINE__);
    if(ierr != PIO_NOERR){
        LOG((1, "nc*_put_att_* failed, ierr = %d", ierr));
        return ierr;
    }

#ifdef TIMING
    GPTLstop("PIO:PIOc_put_att_tc");
#endif
    return PIO_NOERR;
}

/**
 * Get the value of an attribute of any type, converting to any type.
 *
 * This routine is called collectively by all tasks in the communicator
 * ios.union_comm.
 *
 * @param ncid the ncid of the open file, obtained from
 * PIOc_openfile() or PIOc_createfile().
 * @param varid the variable ID.
 * @param name the name of the attribute to get
 * @param memtype the type of the data in memory (if different from
 * the type of the attribute, the data will be converted to
 * memtype). The ip pointer points to memory to hold att_len elements
 * of type memtype.
 * @param ip a pointer that will get the attribute value.
 * @return PIO_NOERR for success, error code otherwise.
 * @author Ed Hartnett
 */
int PIOc_get_att_tc(int ncid, int varid, const char *name, nc_type memtype, void *ip)
{
    iosystem_desc_t *ios;   /* Pointer to io system information. */
    file_desc_t *file;      /* Pointer to file information. */
    nc_type atttype;        /* The type of the attribute. */
    PIO_Offset attlen;      /* Number of elements in the attribute array. */
    PIO_Offset atttype_len; /* Length in bytes of one element of the attribute type. */
    PIO_Offset memtype_len; /* Length in bytes of one element of the memory type. */
    int mpierr = MPI_SUCCESS, mpierr2;  /* Return code from MPI function calls. */
    int ierr = PIO_NOERR;               /* Return code from function calls. */

#ifdef TIMING
    GPTLstart("PIO:PIOc_get_att_tc");
#endif
    /* Find the info about this file. */
    if ((ierr = pio_get_file(ncid, &file)))
        return pio_err(NULL, NULL, ierr, __FILE__, __LINE__);
    ios = file->iosystem;

    /* User must provide a name and destination pointer. */
    if (!name || !ip || strlen(name) > PIO_MAX_NAME)
        return pio_err(ios, file, PIO_EINVAL, __FILE__, __LINE__);

    LOG((1, "PIOc_get_att_tc ncid %d varid %d name %s memtype %d",
         ncid, varid, name, memtype));

    /* Run these on all tasks if async is not in use, but only on
     * non-IO tasks if async is in use. */
    if (!ios->async || !ios->ioproc)
    {
        /* Get the type and length of the attribute. */
        ierr = PIOc_inq_att(ncid, varid, name, &atttype, &attlen);
        if(ierr != PIO_NOERR){
            LOG((1, "PIOc_inq_att failed, ierr = %d", ierr));
            return ierr;
        }
        LOG((2, "atttype = %d attlen = %d", atttype, attlen));

        /* Get the length (in bytes) of the type of the attribute. */
        ierr = PIOc_inq_type(ncid, atttype, NULL, &atttype_len);
        if(ierr != PIO_NOERR){
            LOG((1, "PIOc_inq_type failed, ierr=%d", ierr));
            return ierr;
        }

        /* Get the length (in bytes) of the type that the user wants
         * the data converted to. */
        if (memtype == PIO_LONG_INTERNAL)
            memtype_len = sizeof(long int);
        else
        {
            ierr = PIOc_inq_type(ncid, memtype, NULL, &memtype_len);
            if(ierr != PIO_NOERR){
                LOG((1, "PIOc_inq_type failed, ierr = %d", ierr));
                return ierr;
            }
        }
    }
    LOG((2, "atttype_len = %d memtype_len = %d", atttype_len, memtype_len));

    /* If async is in use, and this is not an IO task, bcast the
     * parameters and the attribute and type information we fetched. */
    if (ios->async)
    {
        int msg = PIO_MSG_GET_ATT;
        int namelen = strlen(name) + 1;
        PIO_SEND_ASYNC_MSG(ios, msg, &ierr, ncid, varid, namelen, name,
            file->iotype, atttype, attlen, atttype_len, memtype, memtype_len);
        if(ierr != PIO_NOERR)
        {
            LOG((1, "Error sending async msg for PIO_MSG_GET_ATT"));
            return pio_err(ios, NULL, ierr, __FILE__, __LINE__);
        }

        /* Broadcast values currently only known on computation tasks to IO tasks. */
        LOG((2, "PIOc_get_att_tc bcast from comproot = %d attlen = %d atttype_len = %d", ios->comproot, attlen, atttype_len));
        if ((mpierr = MPI_Bcast(&attlen, 1, MPI_OFFSET, ios->comproot, ios->my_comm)))
            return check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        if ((mpierr = MPI_Bcast(&atttype_len, 1, MPI_OFFSET, ios->comproot, ios->my_comm)))
            return check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        if ((mpierr = MPI_Bcast(&memtype_len, 1, MPI_OFFSET, ios->comproot, ios->my_comm)))
            return check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        LOG((2, "PIOc_get_att_tc bcast complete attlen = %d atttype_len = %d memtype_len = %d", attlen, atttype_len,
             memtype_len));
    }

    /* If this is an IO task, then call the netCDF function. */
    if (ios->ioproc)
    {
        LOG((2, "calling pnetcdf/netcdf"));
#ifdef _PNETCDF
        if (file->iotype == PIO_IOTYPE_PNETCDF)
        {
            switch(memtype)
            {
            case NC_BYTE:
                ierr = ncmpi_get_att_schar(file->fh, varid, name, ip);
                break;
            case NC_CHAR:
                ierr = ncmpi_get_att_text(file->fh, varid, name, ip);
                break;
            case NC_SHORT:
                ierr = ncmpi_get_att_short(file->fh, varid, name, ip);
                break;
            case NC_INT:
                ierr = ncmpi_get_att_int(file->fh, varid, name, ip);
                break;
            case PIO_LONG_INTERNAL:
                ierr = ncmpi_get_att_long(file->fh, varid, name, ip);
                break;
            case NC_FLOAT:
                ierr = ncmpi_get_att_float(file->fh, varid, name, ip);
                break;
            case NC_DOUBLE:
                ierr = ncmpi_get_att_double(file->fh, varid, name, ip);
                break;
            default:
                return pio_err(ios, file, PIO_EBADTYPE, __FILE__, __LINE__);
            }
        }
#endif /* _PNETCDF */

        if (file->iotype != PIO_IOTYPE_PNETCDF && file->do_io)
        {
            switch(memtype)
            {
#ifdef _NETCDF
            case NC_CHAR:
                ierr = nc_get_att_text(file->fh, varid, name, ip);
                break;
            case NC_BYTE:
                ierr = nc_get_att_schar(file->fh, varid, name, ip);
                break;
            case NC_SHORT:
                ierr = nc_get_att_short(file->fh, varid, name, ip);
                break;
            case NC_INT:
                ierr = nc_get_att_int(file->fh, varid, name, ip);
                break;
            case PIO_LONG_INTERNAL:
                ierr = nc_get_att_long(file->fh, varid, name, ip);
                break;
            case NC_FLOAT:
                ierr = nc_get_att_float(file->fh, varid, name, ip);
                break;
            case NC_DOUBLE:
                ierr = nc_get_att_double(file->fh, varid, name, ip);
                break;
#endif /* _NETCDF */
#ifdef _NETCDF4
            case NC_UBYTE:
                ierr = nc_get_att_uchar(file->fh, varid, name, ip);
                break;
            case NC_USHORT:
                ierr = nc_get_att_ushort(file->fh, varid, name, ip);
                break;
            case NC_UINT:
                ierr = nc_get_att_uint(file->fh, varid, name, ip);
                break;
            case NC_INT64:
                LOG((3, "about to call nc_get_att_longlong"));
                ierr = nc_get_att_longlong(file->fh, varid, name, ip);
                break;
            case NC_UINT64:
                ierr = nc_get_att_ulonglong(file->fh, varid, name, ip);
                break;
                /* case NC_STRING: */
                /*      ierr = nc_get_att_string(file->fh, varid, name, ip); */
                /*      break; */
#endif /* _NETCDF4 */
            default:
                return pio_err(ios, file, PIO_EBADTYPE, __FILE__, __LINE__);
            }
        }
    }

    ierr = check_netcdf(NULL, file, ierr, __FILE__, __LINE__);
    if(ierr != PIO_NOERR){
        LOG((1, "nc*_get_att_* failed, ierr = %d", ierr));
        return ierr;
    }

    /* Broadcast results to all tasks. */
    LOG((2, "bcasting att values attlen = %d memtype_len = %d", attlen, memtype_len));
    if ((mpierr = MPI_Bcast(ip, (int)attlen * memtype_len, MPI_BYTE, ios->ioroot,
                            ios->my_comm)))
        return check_mpi(NULL, file, mpierr, __FILE__, __LINE__);

    LOG((2, "get_att_tc data bcast complete"));
#ifdef TIMING
    GPTLstop("PIO:PIOc_get_att_tc");
#endif
    return PIO_NOERR;
}

/**
 * Internal PIO function which provides a type-neutral interface to
 * nc_get_vars.
 *
 * Users should not call this function directly. Instead, call one of
 * the derived functions, depending on the type of data you are
 * reading: PIOc_get_vars_text(), PIOc_get_vars_uchar(),
 * PIOc_get_vars_schar(), PIOc_get_vars_ushort(),
 * PIOc_get_vars_short(), PIOc_get_vars_uint(), PIOc_get_vars_int(),
 * PIOc_get_vars_long(), PIOc_get_vars_float(),
 * PIOc_get_vars_double(), PIOc_get_vars_ulonglong(),
 * PIOc_get_vars_longlong()
 *
 * This routine is called collectively by all tasks in the
 * communicator ios.union_comm.
 *
 * @param ncid identifies the netCDF file
 * @param varid the variable ID number
 * @param start an array of start indicies (must have same number of
 * entries as variable has dimensions). If NULL, indices of 0 will be
 * used.
 * @param count an array of counts (must have same number of entries
 * as variable has dimensions). If NULL, counts matching the size of
 * the variable will be used.
 * @param stride an array of strides (must have same number of
 * entries as variable has dimensions). If NULL, strides of 1 will be
 * used.
 * @param xtype the netCDF type of the data being passed in buf. Data
 * will be automatically covnerted from the type of the variable being
 * read from to this type. If NC_NAT then the variable's file type
 * will be used. Use special PIO_LONG_INTERNAL for _long() functions.
 * @param buf pointer to the data to be written.
 * @return PIO_NOERR on success, error code otherwise.
 * @author Ed Hartnett
 */
int PIOc_get_vars_tc(int ncid, int varid, const PIO_Offset *start, const PIO_Offset *count,
                     const PIO_Offset *stride, nc_type xtype, void *buf)
{
    iosystem_desc_t *ios;  /* Pointer to io system information. */
    file_desc_t *file;     /* Pointer to file information. */
    int ndims;             /* The number of dimensions in the variable. */
    PIO_Offset typelen;    /* Size (in bytes) of the data type of data in buf. */
    PIO_Offset num_elem = 1; /* Number of data elements in the buffer. */
    nc_type vartype;         /* The type of the var we are reading from. */
    char start_present = start ? true : false;
    char count_present = count ? true : false;
    char stride_present = stride ? true : false;
    int mpierr = MPI_SUCCESS, mpierr2;  /* Return code from MPI function codes. */
    int ierr = PIO_NOERR;                           /* Return code. */

#ifdef TIMING
    GPTLstart("PIO:PIOc_get_vars_tc");
#endif
    LOG((1, "PIOc_get_vars_tc ncid = %d varid = %d xtype = %d start_present = %d "
         "count_present = %d stride_present = %d", ncid, varid, xtype, start_present,
         count_present, stride_present));

    /* Find the info about this file. */
    if ((ierr = pio_get_file(ncid, &file)))
        return pio_err(NULL, NULL, ierr, __FILE__, __LINE__);
    ios = file->iosystem;

    /* User must provide a place to put some data. */
    if (!buf)
        return pio_err(ios, file, PIO_EINVAL, __FILE__, __LINE__);

    /* Run these on all tasks if async is not in use, but only on
     * non-IO tasks if async is in use. */
    if (!ios->async || !ios->ioproc)
    {
        /* Get the type of this var. */
        ierr = PIOc_inq_vartype(ncid, varid, &vartype);
        if(ierr != PIO_NOERR){
            LOG((1, "PIOc_inq_vartype failed, ierr = %d", ierr));
            return ierr;
        }

        /* If no type was specified, use the var type. */
        if (xtype == NC_NAT)
            xtype = vartype;

        /* Handle _long() calls with an special type. */
        if (xtype == PIO_LONG_INTERNAL)
            typelen = sizeof(long int);
        else
        {
            ierr = PIOc_inq_type(ncid, xtype, NULL, &typelen);
            if(ierr != PIO_NOERR){
                LOG((1, "PIOc_inq_type failed, ierr = %d", ierr));
                return ierr;
            }
        }

        /* Get the number of dims for this var. */
        ierr = PIOc_inq_varndims(ncid, varid, &ndims);
        if(ierr != PIO_NOERR){
            LOG((1, "PIOc_inq_varndims failed, ierr = %d", ierr));
            return ierr;
        }
        LOG((3, "ndims = %d", ndims));

        /* Only scalar vars can pass NULL for start/count. */
        pioassert(ndims == 0 || (start && count), "need start/count", __FILE__, __LINE__);

        /* How many elements in buf? (For scalars, ndims is 0 and
         * num_elem will remain 1). */
        for (int vd = 0; vd < ndims; vd++)
            num_elem *= count[vd];
        LOG((2, "PIOc_get_vars_tc num_elem = %d", num_elem));
    }

    /* If async is in use, and this is not an IO task, bcast the parameters. */
    if (ios->async)
    {
        int msg = PIO_MSG_GET_VARS;
        PIO_Offset *amsg_startp = NULL, *amsg_countp = NULL, *amsg_stridep = NULL;
        /* Handle scalars too, ndims == 0 */
        int start_sz = (ndims > 0) ? ndims : 1;
        int count_sz = (ndims > 0) ? ndims : 1;
        int stride_sz = (ndims > 0) ? ndims : 1;
        if(!start_present)
        {
            amsg_startp = calloc(start_sz, sizeof(PIO_Offset));
        }
        if(!count_present)
        {
            amsg_countp = calloc(count_sz, sizeof(PIO_Offset));
        }
        if(!stride_present)
        {
            amsg_stridep = calloc(stride_sz, sizeof(PIO_Offset));
        }

        PIO_SEND_ASYNC_MSG(ios, msg, &ierr, ncid, varid, ndims,
                            start_present, start_sz,
                            (start_present) ? start : amsg_startp, 
                            count_present, count_sz,
                            (count_present) ? count : amsg_countp, 
                            stride_present, stride_sz,
                            (stride_present) ? stride : amsg_stridep,
                            xtype, num_elem, typelen);
        if(ierr != PIO_NOERR)
        {
            LOG((1, "Error sending async msg for PIO_MSG_GET_VARS"));
            return pio_err(ios, NULL, ierr, __FILE__, __LINE__);
        }

        if(!start_present)
        {
            free(amsg_startp);
        }
        if(!count_present)
        {
            free(amsg_countp);
        }
        if(!stride_present)
        {
            free(amsg_stridep);
        }

        /* Broadcast values currently only known on computation tasks to IO tasks. */
        if ((mpierr = MPI_Bcast(&num_elem, 1, MPI_OFFSET, ios->comproot, ios->my_comm)))
            check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        if ((mpierr = MPI_Bcast(&typelen, 1, MPI_OFFSET, ios->comproot, ios->my_comm)))
            check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        if ((mpierr = MPI_Bcast(&xtype, 1, MPI_INT, ios->comproot, ios->my_comm)))
            check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
    }

    /* If this is an IO task, then call the netCDF function. */
    if (ios->ioproc)
    {
        LOG((2, "file->iotype = %d xtype = %d file->do_io = %d", file->iotype, xtype, file->do_io));
#ifdef _PNETCDF
        if (file->iotype == PIO_IOTYPE_PNETCDF)
        {
            LOG((2, "pnetcdf calling ncmpi_get_vars_*() file->fh = %d varid = %d", file->fh, varid));
            /* Turn on independent access for pnetcdf file. */
            if ((ierr = ncmpi_begin_indep_data(file->fh)))
                return pio_err(ios, file, ierr, __FILE__, __LINE__);

            /* Only the IO master does the IO, so we are not really
             * getting parallel IO here. */
            if (ios->iomaster == MPI_ROOT)
            {
                switch(xtype)
                {
                case NC_BYTE:
                    ierr = ncmpi_get_vars_schar(file->fh, varid, start, count, stride, buf);
                    break;
                case NC_CHAR:
                    ierr = ncmpi_get_vars_text(file->fh, varid, start, count, stride, buf);
                    break;
                case NC_SHORT:
                    ierr = ncmpi_get_vars_short(file->fh, varid, start, count, stride, buf);
                    break;
                case NC_INT:
                    ierr = ncmpi_get_vars_int(file->fh, varid, start, count, stride, buf);
                    break;
                case PIO_LONG_INTERNAL:
                    ierr = ncmpi_get_vars_long(file->fh, varid, start, count, stride, buf);
                    break;
                case NC_FLOAT:
                    ierr = ncmpi_get_vars_float(file->fh, varid, start, count, stride, buf);
                    break;
                case NC_DOUBLE:
                    ierr = ncmpi_get_vars_double(file->fh, varid, start, count, stride, buf);
                    break;
                default:
                    return pio_err(ios, file, PIO_EBADIOTYPE, __FILE__, __LINE__);
                }
            }

            /* Turn off independent access for pnetcdf file. */
            if ((ierr = ncmpi_end_indep_data(file->fh)))
                return pio_err(ios, file, ierr, __FILE__, __LINE__);
        }
#endif /* _PNETCDF */

        if (file->iotype != PIO_IOTYPE_PNETCDF && file->do_io)
            switch(xtype)
            {
#ifdef _NETCDF
            case NC_BYTE:
                ierr = nc_get_vars_schar(file->fh, varid, (size_t *)start, (size_t *)count,
                                         (ptrdiff_t *)stride, buf);
                break;
            case NC_CHAR:
                ierr = nc_get_vars_text(file->fh, varid, (size_t *)start, (size_t *)count,
                                        (ptrdiff_t *)stride, buf);
                break;
            case NC_SHORT:
                ierr = nc_get_vars_short(file->fh, varid, (size_t *)start, (size_t *)count,
                                         (ptrdiff_t *)stride, buf);
                break;
            case NC_INT:
                ierr = nc_get_vars_int(file->fh, varid, (size_t *)start, (size_t *)count,
                                       (ptrdiff_t *)stride, buf);
                break;
            case PIO_LONG_INTERNAL:
                ierr = nc_get_vars_long(file->fh, varid, (size_t *)start, (size_t *)count,
                                        (ptrdiff_t *)stride, buf);
                break;
            case NC_FLOAT:
                ierr = nc_get_vars_float(file->fh, varid, (size_t *)start, (size_t *)count,
                                         (ptrdiff_t *)stride, buf);
                break;
            case NC_DOUBLE:
                ierr = nc_get_vars_double(file->fh, varid, (size_t *)start, (size_t *)count,
                                          (ptrdiff_t *)stride, buf);
                break;
#endif
#ifdef _NETCDF4
            case NC_UBYTE:
                ierr = nc_get_vars_uchar(file->fh, varid, (size_t *)start, (size_t *)count,
                                         (ptrdiff_t *)stride, buf);
                break;
            case NC_USHORT:
                ierr = nc_get_vars_ushort(file->fh, varid, (size_t *)start, (size_t *)count,
                                          (ptrdiff_t *)stride, buf);
                break;
            case NC_UINT:
                ierr = nc_get_vars_uint(file->fh, varid, (size_t *)start, (size_t *)count,
                                        (ptrdiff_t *)stride, buf);
                break;
            case NC_INT64:
                LOG((3, "about to call nc_get_vars_longlong"));
                ierr = nc_get_vars_longlong(file->fh, varid, (size_t *)start, (size_t *)count,
                                            (ptrdiff_t *)stride, buf);
                break;
            case NC_UINT64:
                ierr = nc_get_vars_ulonglong(file->fh, varid, (size_t *)start, (size_t *)count,
                                             (ptrdiff_t *)stride, buf);
                break;
                /* case NC_STRING: */
                /*      ierr = nc_get_vars_string(file->fh, varid, (size_t *)start, (size_t *)count, */
                /*                                (ptrdiff_t *)stride, (void *)buf); */
                /*      break; */
#endif /* _NETCDF4 */
            default:
                return pio_err(ios, file, PIO_EBADTYPE, __FILE__, __LINE__);
            }
    }

    ierr = check_netcdf(NULL, file, ierr, __FILE__, __LINE__);
    if(ierr != PIO_NOERR){
        LOG((1, "nc*_get_vars_* failed, ierr = %d", ierr));
        return ierr;
    }

    /* Send the data. */
    LOG((2, "PIOc_get_vars_tc bcasting data num_elem = %d typelen = %d ios->ioroot = %d", num_elem,
         typelen, ios->ioroot));
    if ((mpierr = MPI_Bcast(buf, num_elem * typelen, MPI_BYTE, ios->ioroot, ios->my_comm)))
        return check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
    LOG((2, "PIOc_get_vars_tc bcasting data complete"));

#ifdef TIMING
    GPTLstop("PIO:PIOc_get_vars_tc");
#endif
    return PIO_NOERR;
}

/**
 * Get one value of a variable of any type. This is an internal
 * function.
 *
 * This routine is called collectively by all tasks in the
 * communicator ios.union_comm.
 *
 * @param ncid identifies the netCDF file
 * @param varid the variable ID number
 * @param index an array of start indicies (must have same number of
 * entries as variable has dimensions). If NULL, indices of 0 will be
 * used.
 * @param xtype the netcdf type of the variable.
 * @param buf pointer that will get the data.
 * @return PIO_NOERR on success, error code otherwise.
 * @author Ed Hartnett
 */
int PIOc_get_var1_tc(int ncid, int varid, const PIO_Offset *index, nc_type xtype,
                     void *buf)
{
    iosystem_desc_t *ios;  /* Pointer to io system information. */
    file_desc_t *file;     /* Pointer to file information. */
    int ndims;   /* The number of dimensions in the variable. */
    int ierr = PIO_NOERR;    /* Return code from function calls. */

    /* Find the info about this file. We need this for error handling. */
    if ((ierr = pio_get_file(ncid, &file)))
        return pio_err(NULL, NULL, ierr, __FILE__, __LINE__);
    ios = file->iosystem;

    /* Find the number of dimensions. */
    if ((ierr = PIOc_inq_varndims(ncid, varid, &ndims)))
        return pio_err(ios, file, ierr, __FILE__, __LINE__);

    /* Set up count array. */
    PIO_Offset count[ndims];
    for (int c = 0; c < ndims; c++)
        count[c] = 1;

    return PIOc_get_vars_tc(ncid, varid, index, count, NULL, xtype, buf);
}

/**
 * Get a complete variable of any type. This is an internal function.
 *
 * This routine is called collectively by all tasks in the
 * communicator ios.union_comm.
 *
 * @param ncid identifies the netCDF file
 * @param varid the variable ID number
 * @param index an array of start indicies (must have same number of
 * entries as variable has dimensions). If NULL, indices of 0 will be
 * used.
 * @param xtype the netcdf type of the variable.
 * @param buf pointer that will get the data.
 * @return PIO_NOERR on success, error code otherwise.
 * @author Ed Hartnett
 */
int PIOc_get_var_tc(int ncid, int varid, nc_type xtype, void *buf)
{
    iosystem_desc_t *ios;  /* Pointer to io system information. */
    file_desc_t *file;     /* Pointer to file information. */
    PIO_Offset *startp = NULL; /* Pointer to start array. */
    PIO_Offset *countp = NULL; /* Pointer to count array. */
    int ndims;   /* The number of dimensions in the variable. */
    PIO_Offset my_start[PIO_MAX_DIMS];
    PIO_Offset dimlen[PIO_MAX_DIMS];
    int ierr = PIO_NOERR;    /* Return code from function calls. */

    LOG((1, "PIOc_get_var_tc ncid = %d varid = %d xtype = %d", ncid, varid,
         xtype));

    /* Find the info about this file. We need this for error handling. */
    if ((ierr = pio_get_file(ncid, &file)))
        return pio_err(NULL, NULL, ierr, __FILE__, __LINE__);
    ios = file->iosystem;

    /* Find the number of dimensions. */
    if ((ierr = PIOc_inq_varndims(ncid, varid, &ndims)))
        return pio_err(ios, file, ierr, __FILE__, __LINE__);

    /* Scalar vars (which have ndims == 0) should just pass NULLs for
     * start/count. */
    if (ndims)
    {
        /* Find the dimension IDs. */
        int dimids[ndims];
        if ((ierr = PIOc_inq_vardimid(ncid, varid, dimids)))
            return pio_err(ios, file, ierr, __FILE__, __LINE__);

        /* Find the dimension lengths. */
        for (int d = 0; d < ndims; d++)
            if ((ierr = PIOc_inq_dimlen(ncid, dimids[d], &dimlen[d])))
                return pio_err(ios, file, ierr, __FILE__, __LINE__);

        /* Set up start array. */
        for (int d = 0; d < ndims; d++)
        {
            my_start[d] = 0;
            LOG((3, "my_start[%d] = %d dimlen[%d] = %d", d, my_start[d], d,
                 dimlen[d]));
        }

        /* Set the start/count arrays. */
        startp = my_start;
        countp = dimlen;
    }

    return PIOc_get_vars_tc(ncid, varid, startp, countp, NULL, xtype, buf);
}

/**
 * Internal PIO function which provides a type-neutral interface to
 * nc_put_vars.
 *
 * Users should not call this function directly. Instead, call one of
 * the derived functions, depending on the type of data you are
 * writing: PIOc_put_vars_text(), PIOc_put_vars_uchar(),
 * PIOc_put_vars_schar(), PIOc_put_vars_ushort(),
 * PIOc_put_vars_short(), PIOc_put_vars_uint(), PIOc_put_vars_int(),
 * PIOc_put_vars_long(), PIOc_put_vars_float(),
 * PIOc_put_vars_longlong(), PIOc_put_vars_double(),
 * PIOc_put_vars_ulonglong().
 *
 * This routine is called collectively by all tasks in the
 * communicator ios.union_comm.
 *
 * @param ncid identifies the netCDF file
 * @param varid the variable ID number
 * @param start an array of start indicies (must have same number of
 * entries as variable has dimensions). If NULL, indices of 0 will be
 * used.
 * @param count an array of counts (must have same number of entries
 * as variable has dimensions). If NULL, counts matching the size of
 * the variable will be used.
 * @param stride an array of strides (must have same number of
 * entries as variable has dimensions). If NULL, strides of 1 will be
 * used.
 * @param xtype the netCDF type of the data being passed in buf. Data
 * will be automatically covnerted from this type to the type of the
 * variable being written to. If NC_NAT then the variable's file type
 * will be used. Use special PIO_LONG_INTERNAL for _long() functions.
 * @param buf pointer to the data to be written.
 *
 * @return PIO_NOERR on success, error code otherwise.
 * @author Ed Hartnett
 */
int PIOc_put_vars_tc(int ncid, int varid, const PIO_Offset *start, const PIO_Offset *count,
                     const PIO_Offset *stride, nc_type xtype, const void *buf)
{
    iosystem_desc_t *ios;  /* Pointer to io system information. */
    file_desc_t *file;  /* Pointer to file information. */
    int ndims;          /* The number of dimensions in the variable. */
    PIO_Offset typelen; /* Size (in bytes) of the data type of data in buf. */
    PIO_Offset num_elem = 1; /* Number of data elements in the buffer. */
    char start_present = start ? true : false;    /* Is start non-NULL? */
    char count_present = count ? true : false;    /* Is count non-NULL? */
    char stride_present = stride ? true : false;  /* Is stride non-NULL? */
    var_desc_t *vdesc;
    int *request;
    nc_type vartype;   /* The type of the var we are reading from. */
    int mpierr = MPI_SUCCESS, mpierr2;  /* Return code from MPI function codes. */
    int ierr = PIO_NOERR;          /* Return code from function calls. */

#ifdef TIMING
    GPTLstart("PIO:PIOc_put_vars_tc");
#endif
    LOG((1, "PIOc_put_vars_tc ncid = %d varid = %d start_present = %d "
         "count_present = %d stride_present = %d xtype = %d", ncid, varid,
         start_present, count_present, stride_present, xtype));

    /* Get file info. */
    if ((ierr = pio_get_file(ncid, &file)))
        return pio_err(NULL, NULL, ierr, __FILE__, __LINE__);
    ios = file->iosystem;

    /* User must provide a place to put some data. */
    if (!buf)
        return pio_err(ios, file, PIO_EINVAL, __FILE__, __LINE__);

    /* Run these on all tasks if async is not in use, but only on
     * non-IO tasks if async is in use. */
    if (!ios->async || !ios->ioproc)
    {
        /* Get the type of this var. */
        ierr = PIOc_inq_vartype(ncid, varid, &vartype);
        if(ierr != PIO_NOERR){
            LOG((1, "PIOc_inq_vartype failed, ierr = %d", ierr));
            return ierr;
        }

        /* If no type was specified, use the var type. */
        if (xtype == NC_NAT)
            xtype = vartype;

        /* Get the number of dims for this var. */
        ierr = PIOc_inq_varndims(ncid, varid, &ndims);
        if(ierr != PIO_NOERR){
            LOG((1, "PIOc_inq_varndims failed, ierr = %d", ierr));
            return ierr;
        }

        /* Get the length of the data type. */
        if (xtype == PIO_LONG_INTERNAL)
            typelen = sizeof(long int);
        else
        {
            ierr = PIOc_inq_type(ncid, xtype, NULL, &typelen);
            if(ierr != PIO_NOERR){
                LOG((1, "PIOc_inq_type failed, ierr = %d", ierr));
                return ierr;
            }
        }

        LOG((2, "ndims = %d typelen = %d", ndims, typelen));

        /* How many elements of data? If no count array was passed,
         * this is a scalar. */
        if (count)
            for (int vd = 0; vd < ndims; vd++)
                num_elem *= count[vd];
    }

    /* If async is in use, and this is not an IO task, bcast the parameters. */
    if (ios->async)
    {
        int msg = PIO_MSG_PUT_VARS;
        PIO_Offset *amsg_startp = NULL, *amsg_countp = NULL, *amsg_stridep = NULL;
        /* Make sure we handle scalars too, ndims == 0 */
        int start_sz = (ndims > 0) ? ndims : 1;
        int count_sz = (ndims > 0) ? ndims : 1;
        int stride_sz = (ndims > 0) ? ndims : 1;
        if(!start_present)
        {
            amsg_startp = calloc(start_sz, sizeof(PIO_Offset));
        }
        if(!count_present)
        {
            amsg_countp = calloc(count_sz, sizeof(PIO_Offset));
        }
        if(!stride_present)
        {
            amsg_stridep = calloc(stride_sz, sizeof(PIO_Offset));
        }

        PIO_SEND_ASYNC_MSG(ios, msg, &ierr, ncid, varid, ndims,
                            start_present, start_sz,
                            (start_present) ? start : amsg_startp, 
                            count_present, count_sz,
                            (count_present) ? count : amsg_countp, 
                            stride_present, stride_sz,
                            (stride_present) ? stride : amsg_stridep,
                            xtype, num_elem, typelen,
                            num_elem * typelen, buf); 
        if(ierr != PIO_NOERR)
        {
            LOG((1, "Error sending async msg for PIO_MSG_PUT_VARS"));
            return pio_err(ios, NULL, ierr, __FILE__, __LINE__);
        }

        if(!start_present)
        {
            free(amsg_startp);
        }
        if(!count_present)
        {
            free(amsg_countp);
        }
        if(!stride_present)
        {
            free(amsg_stridep);
        }

        /* Broadcast values currently only known on computation tasks to IO tasks. */
        LOG((2, "PIOc_put_vars_tc bcast from comproot"));
        if ((mpierr = MPI_Bcast(&ndims, 1, MPI_INT, ios->comproot, ios->my_comm)))
            return check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        if ((mpierr = MPI_Bcast(&xtype, 1, MPI_INT, ios->comproot, ios->my_comm)))
            return check_mpi(NULL, file, mpierr, __FILE__, __LINE__);
        LOG((2, "PIOc_put_vars_tc complete bcast from comproot ndims = %d", ndims));
    }

    /* If this is an IO task, then call the netCDF function. */
    if (ios->ioproc)
    {
#ifdef _PNETCDF
        if (file->iotype == PIO_IOTYPE_PNETCDF)
        {
            LOG((2, "PIOc_put_vars_tc calling pnetcdf function"));
            vdesc = &file->varlist[varid];
            if (vdesc->nreqs % PIO_REQUEST_ALLOC_CHUNK == 0)
                if (!(vdesc->request = realloc(vdesc->request,
                                               sizeof(int) * (vdesc->nreqs + PIO_REQUEST_ALLOC_CHUNK))))
                    return pio_err(ios, file, PIO_ENOMEM, __FILE__, __LINE__);
            request = vdesc->request + vdesc->nreqs;
            LOG((2, "PIOc_put_vars_tc request = %d", vdesc->request));

            /* Scalars have to be handled differently. */
            if (ndims == 0)
            {
                /* This is a scalar var. */
                LOG((2, "pnetcdf writing scalar with ncmpi_put_vars_*() file->fh = %d varid = %d",
                     file->fh, varid));
                pioassert(!start && !count && !stride, "expected NULLs", __FILE__, __LINE__);

                /* Only the IO master does the IO, so we are not really
                 * getting parallel IO here. */
                if (ios->iomaster == MPI_ROOT)
                {
                    switch(xtype)
                    {
                    case NC_BYTE:
                        ierr = ncmpi_bput_var_schar(file->fh, varid, buf, request);
                        break;
                    case NC_CHAR:
                        ierr = ncmpi_bput_var_text(file->fh, varid, buf, request);
                        break;
                    case NC_SHORT:
                        ierr = ncmpi_bput_var_short(file->fh, varid, buf, request);
                        break;
                    case NC_INT:
                        ierr = ncmpi_bput_var_int(file->fh, varid, buf, request);
                        break;
                    case PIO_LONG_INTERNAL:
                        ierr = ncmpi_bput_var_long(file->fh, varid, buf, request);
                        break;
                    case NC_FLOAT:
                        ierr = ncmpi_bput_var_float(file->fh, varid, buf, request);
                        break;
                    case NC_DOUBLE:
                        ierr = ncmpi_bput_var_double(file->fh, varid, buf, request);
                        break;
                    default:
                        return pio_err(ios, file, PIO_EBADIOTYPE, __FILE__, __LINE__);
                    }
                    LOG((2, "PIOc_put_vars_tc io_rank 0 done with pnetcdf call, ierr=%d", ierr));
                }
                else
                    *request = PIO_REQ_NULL;

                vdesc->nreqs++;
                flush_output_buffer(file, false, 0);
                LOG((2, "PIOc_put_vars_tc flushed output buffer"));
            }
            else
            {
                /* This is not a scalar var. */
                PIO_Offset *fake_stride;

                if (!stride_present)
                {
                    LOG((2, "stride not present"));
                    if (!(fake_stride = malloc(ndims * sizeof(PIO_Offset))))
                        return pio_err(ios, file, PIO_ENOMEM, __FILE__, __LINE__);
                    for (int d = 0; d < ndims; d++)
                        fake_stride[d] = 1;
                }
                else
                    fake_stride = (PIO_Offset *)stride;

                /* Only the IO master actually does the call. */
                if (ios->iomaster == MPI_ROOT)
                {
                    switch(xtype)
                    {
                    case NC_BYTE:
                        ierr = ncmpi_bput_vars_schar(file->fh, varid, start, count, fake_stride, buf, request);
                        break;
                    case NC_CHAR:
                        ierr = ncmpi_bput_vars_text(file->fh, varid, start, count, fake_stride, buf, request);
                        break;
                    case NC_SHORT:
                        ierr = ncmpi_bput_vars_short(file->fh, varid, start, count, fake_stride, buf, request);
                        break;
                    case NC_INT:
                        ierr = ncmpi_bput_vars_int(file->fh, varid, start, count, fake_stride, buf, request);
                        break;
                    case PIO_LONG_INTERNAL:
                        ierr = ncmpi_bput_vars_long(file->fh, varid, start, count, fake_stride, buf, request);
                        break;
                    case NC_FLOAT:
                        ierr = ncmpi_bput_vars_float(file->fh, varid, start, count, fake_stride, buf, request);
                        break;
                    case NC_DOUBLE:
                        ierr = ncmpi_bput_vars_double(file->fh, varid, start, count, fake_stride, buf, request);
                        break;
                    default:
                        return pio_err(ios, file, PIO_EBADTYPE, __FILE__, __LINE__);
                    }
                    LOG((2, "PIOc_put_vars_tc io_rank 0 done with pnetcdf call, ierr=%d", ierr));
                }
                else
                    *request = PIO_REQ_NULL;

                vdesc->nreqs++;
                flush_output_buffer(file, false, 0);
                LOG((2, "PIOc_put_vars_tc flushed output buffer"));

                /* Free malloced resources. */
                if (!stride_present)
                    free(fake_stride);
            } /* endif ndims == 0 */
        }
#endif /* _PNETCDF */

#ifdef _ADIOS
        if (file->iotype == PIO_IOTYPE_ADIOS)
        {
            if (varid < 0 || varid >= file->num_vars)
                return pio_err(file->iosystem, file, PIO_EBADID, __FILE__, __LINE__);
            /* First we need to define the variable now that we know it's decomposition */
            adios_var_desc_t * av = &(file->adios_vars[varid]);

            /* Scalars have to be handled differently. */
            if (av->ndims == 0)
            {
                /* This is a scalar var. */
                LOG((2, "ADIOS writing scalar file->fh = %d varid = %d",
                        file->fh, varid));
                pioassert(!start && !count && !stride, "expected NULLs", __FILE__, __LINE__);

                /* Only the IO master does the IO, so we are not really
                 * getting parallel IO here. */
                if (ios->iomaster == MPI_ROOT)
                {
                    int64_t vid = adios_define_var(file->adios_group, av->name, "", av->adios_type,
                            "","","");
                    adios_write_byid(file->adios_fh, vid, buf);
                }
            }
            else
            {
                /* This is not a scalar var. */
                if (stride_present)
                {
                    fprintf(stderr,"ADIOS does not support striping %s:%s\n"
                            "Variable %s will be corrupted in the output\n"
                            , __FILE__, __func__, av->name);
                }

                char ldims[32],gdims[256],offs[256],tmp[256];
                ldims[0] = '\0';
                for (int d=0; d < av->ndims; d++)
                {
                    sprintf(tmp,"%lld", count[d]);
                    strcat(ldims,tmp);
                    if (d < av->ndims-1)
                        strcat(ldims,",");
                }
                gdims[0] = '\0';
                for (int d=0; d < av->ndims; d++)
                {
                    char dimname[128];
                    snprintf(dimname, sizeof(dimname), "/__pio__/dim/%s", file->dim_names[av->gdimids[d]]);
                    strcat(gdims,dimname);
                    if (d < av->ndims-1)
                        strcat(gdims,",");
                }
                offs[0] = '\0';
                for (int d=0; d < av->ndims; d++)
                {
                    sprintf(tmp,"%lld", start[d]);
                    strcat(offs,tmp);
                    if (d < av->ndims-1)
                        strcat(offs,",");
                }
                fprintf(stderr,"ADIOS variable %s on io rank %d define gdims=\"%s\", ldims=\"%s\", offsets=\"%s\"\n",
                                            av->name, ios->io_rank, gdims, ldims, offs);
                int64_t vid = adios_define_var(file->adios_group, av->name, "", av->adios_type, ldims,gdims,offs);
                adios_write_byid(file->adios_fh, vid, buf);
                char* dimnames[6];
                for (int i = 0; i < av->ndims; i++)
                {
                    dimnames[i] = file->dim_names[av->gdimids[i]];
                }
                adios_define_attribute_byvalue(file->adios_group,"__pio__/dims",av->name,adios_string_array,av->ndims,dimnames);
            }

            if (ios->iomaster == MPI_ROOT)
            {
                adios_define_attribute_byvalue(file->adios_group,"__pio__/ndims",av->name,adios_integer,1,&av->ndims);
                adios_define_attribute_byvalue(file->adios_group,"__pio__/nctype",av->name,adios_integer,1,&av->nc_type);
                adios_define_attribute(file->adios_group, "__pio__/ncop", av->name, adios_string, "put_var", NULL);
            }
        }
#endif

        if (file->iotype != PIO_IOTYPE_PNETCDF && file->iotype != PIO_IOTYPE_ADIOS && file->do_io)
        {
            LOG((2, "PIOc_put_vars_tc calling netcdf function file->iotype = %d",
                 file->iotype));
            switch(xtype)
            {
#ifdef _NETCDF
            case NC_BYTE:
                ierr = nc_put_vars_schar(file->fh, varid, (size_t *)start, (size_t *)count,
                                         (ptrdiff_t *)stride, buf);
                break;
            case NC_CHAR:
                ierr = nc_put_vars_text(file->fh, varid, (size_t *)start, (size_t *)count,
                                        (ptrdiff_t *)stride, buf);
                break;
            case NC_SHORT:
                ierr = nc_put_vars_short(file->fh, varid, (size_t *)start, (size_t *)count,
                                         (ptrdiff_t *)stride, buf);
                break;
            case NC_INT:
                ierr = nc_put_vars_int(file->fh, varid, (size_t *)start, (size_t *)count,
                                       (ptrdiff_t *)stride, buf);
                break;
            case PIO_LONG_INTERNAL:
                ierr = nc_put_vars_long(file->fh, varid, (size_t *)start, (size_t *)count,
                                        (ptrdiff_t *)stride, buf);
                break;
            case NC_FLOAT:
                ierr = nc_put_vars_float(file->fh, varid, (size_t *)start, (size_t *)count,
                                         (ptrdiff_t *)stride, buf);
                break;
            case NC_DOUBLE:
                ierr = nc_put_vars_double(file->fh, varid, (size_t *)start, (size_t *)count,
                                          (ptrdiff_t *)stride, buf);
                break;
#endif
#ifdef _NETCDF4
            case NC_UBYTE:
                ierr = nc_put_vars_uchar(file->fh, varid, (size_t *)start, (size_t *)count,
                                         (ptrdiff_t *)stride, buf);
                break;
            case NC_USHORT:
                ierr = nc_put_vars_ushort(file->fh, varid, (size_t *)start, (size_t *)count,
                                          (ptrdiff_t *)stride, buf);
                break;
            case NC_UINT:
                ierr = nc_put_vars_uint(file->fh, varid, (size_t *)start, (size_t *)count,
                                        (ptrdiff_t *)stride, buf);
                break;
            case NC_INT64:
                ierr = nc_put_vars_longlong(file->fh, varid, (size_t *)start, (size_t *)count,
                                            (ptrdiff_t *)stride, buf);
                break;
            case NC_UINT64:
                ierr = nc_put_vars_ulonglong(file->fh, varid, (size_t *)start, (size_t *)count,
                                             (ptrdiff_t *)stride, buf);
                break;
                /* case NC_STRING: */
                /*      ierr = nc_put_vars_string(file->fh, varid, (size_t *)start, (size_t *)count, */
                /*                                (ptrdiff_t *)stride, (void *)buf); */
                /*      break; */
#endif /* _NETCDF4 */
            default:
                return pio_err(ios, file, PIO_EBADTYPE, __FILE__, __LINE__);
            }
            LOG((2, "PIOc_put_vars_tc io_rank 0 done with netcdf call, ierr=%d", ierr));
        }
    }

    ierr = check_netcdf(NULL, file, ierr, __FILE__, __LINE__);
    if(ierr != PIO_NOERR){
        LOG((1, "nc*_put_vars_* failed, ierr = %d", ierr));
        return ierr;
    }

    LOG((2, "PIOc_put_vars_tc bcast netcdf return code %d complete", ierr));

#ifdef TIMING
    GPTLstop("PIO:PIOc_put_vars_tc");
#endif
    return PIO_NOERR;
}

/**
 * Internal PIO function which provides a type-neutral interface to
 * nc_put_var1 calls.
 *
 * Users should not call this function directly. Instead, call one of
 * the derived functions, depending on the type of data you are
 * writing: PIOc_put_var1_text(), PIOc_put_var1_uchar(),
 * PIOc_put_var1_schar(), PIOc_put_var1_ushort(),
 * PIOc_put_var1_short(), PIOc_put_var1_uint(), PIOc_put_var1_int(),
 * PIOc_put_var1_long(), PIOc_put_var1_float(),
 * PIOc_put_var1_longlong(), PIOc_put_var1_double(),
 * PIOc_put_var1_ulonglong().
 *
 * This routine is called collectively by all tasks in the
 * communicator ios.union_comm.
 *
 * @param ncid identifies the netCDF file
 * @param varid the variable ID number
 * @param index an array of start indicies (must have same number of
 * entries as variable has dimensions). If NULL, indices of 0 will be
 * used.
 * @param xtype the netCDF type of the data being passed in buf. Data
 * will be automatically covnerted from this type to the type of the
 * variable being written to.
 * @param op pointer to the data to be written.
 *
 * @return PIO_NOERR on success, error code otherwise.
 * @author Ed Hartnett
 */
int PIOc_put_var1_tc(int ncid, int varid, const PIO_Offset *index, nc_type xtype,
                     const void *op)
{
    iosystem_desc_t *ios;  /* Pointer to io system information. */
    file_desc_t *file;     /* Pointer to file information. */
    int ndims;   /* The number of dimensions in the variable. */
    int ierr = PIO_NOERR;    /* Return code from function calls. */

    /* Find the info about this file. We need this for error handling. */
    if ((ierr = pio_get_file(ncid, &file)))
        return pio_err(NULL, NULL, ierr, __FILE__, __LINE__);
    ios = file->iosystem;

    /* Find the number of dimensions. */
    if ((ierr = PIOc_inq_varndims(ncid, varid, &ndims)))
        return pio_err(ios, file, ierr, __FILE__, __LINE__);

    /* Set up count array. */
    PIO_Offset count[ndims];
    for (int c = 0; c < ndims; c++)
        count[c] = 1;

    return PIOc_put_vars_tc(ncid, varid, index, count, NULL, xtype, op);
}

/**
 * Internal PIO function which provides a type-neutral interface to
 * nc_put_var calls.
 *
 * Users should not call this function directly. Instead, call one of
 * the derived functions, depending on the type of data you are
 * writing: PIOc_put_var_text(), PIOc_put_var_uchar(),
 * PIOc_put_var_schar(), PIOc_put_var_ushort(),
 * PIOc_put_var_short(), PIOc_put_var_uint(), PIOc_put_var_int(),
 * PIOc_put_var_long(), PIOc_put_var_float(),
 * PIOc_put_var_longlong(), PIOc_put_var_double(),
 * PIOc_put_var_ulonglong().
 *
 * This routine is called collectively by all tasks in the
 * communicator ios.union_comm.
 *
 * @param ncid identifies the netCDF file
 * @param varid the variable ID number
 * @param xtype the netCDF type of the data being passed in buf. Data
 * will be automatically covnerted from this type to the type of the
 * variable being written to.
 * @param op pointer to the data to be written.
 *
 * @return PIO_NOERR on success, error code otherwise.
 * @author Ed Hartnett
 */
int PIOc_put_var_tc(int ncid, int varid, nc_type xtype, const void *op)
{
    iosystem_desc_t *ios;  /* Pointer to io system information. */
    file_desc_t *file;     /* Pointer to file information. */
    PIO_Offset *startp = NULL; /* Pointer to start array. */
    PIO_Offset *countp = NULL; /* Pointer to count array. */
    PIO_Offset start[PIO_MAX_DIMS];
    PIO_Offset count[PIO_MAX_DIMS];
    int ndims;   /* The number of dimensions in the variable. */
    int ierr = PIO_NOERR;    /* Return code from function calls. */

    LOG((1, "PIOc_put_var_tc ncid = %d varid = %d xtype = %d", ncid,
         varid, xtype));

    /* Find the info about this file. We need this for error handling. */
    if ((ierr = pio_get_file(ncid, &file)))
        return pio_err(NULL, NULL, ierr, __FILE__, __LINE__);
    ios = file->iosystem;

    /* Find the number of dimensions. */
    if ((ierr = PIOc_inq_varndims(ncid, varid, &ndims)))
        return pio_err(ios, file, ierr, __FILE__, __LINE__);

    /* Scalar vars (which have ndims == 0) should just pass NULLs for
     * start/count. */
    if (ndims)
    {
        int dimid[ndims];

        /* Set up start array. */
        for (int d = 0; d < ndims; d++)
            start[d] = 0;

        /* Get the dimids for this var. */
        ierr = PIOc_inq_vardimid(ncid, varid, dimid);
        if(ierr != PIO_NOERR){
            LOG((1, "PIOc_inq_vardimid failed, ierr = %d", ierr));
            return ierr;
        }

        /* Count array are the dimlens. */
        for (int d = 0; d < ndims; d++)
            if ((ierr = PIOc_inq_dimlen(ncid, dimid[d], &count[d])))
                return pio_err(ios, file, ierr, __FILE__, __LINE__);

        /* Set the array pointers. */
        startp = start;
        countp = count;
    }

    return PIOc_put_vars_tc(ncid, varid, startp, countp, NULL, xtype, op);
}
