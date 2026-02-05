#include "PdbAstBuilderSwift.h"

#include "PdbUtil.h"
#include "SymbolFileNativePDB.h"

#include "Plugins/TypeSystem/Swift/TypeSystemSwiftTypeRef.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/LLDBAssert.h"

#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"

using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace llvm::codeview;
using namespace llvm::pdb;

PdbAstBuilderSwift::PdbAstBuilderSwift(TypeSystemSwiftTypeRef &swift_ts)
    : m_swift_ts(swift_ts) {}

CompilerType PdbAstBuilderSwift::CreateType(PdbTypeSymId type,
                                            TpiStream &tpi) {
  if (type.index.isSimple())
    return {};

  CVType cvt = tpi.getType(type.index);

  llvm::StringRef mangled_name;
  switch (cvt.kind()) {
  case LF_STRUCTURE:
  case LF_CLASS: {
    ClassRecord cr;
    llvm::cantFail(TypeDeserializer::deserializeAs<ClassRecord>(cvt, cr));
    if (!cr.hasUniqueName())
      return {};
    mangled_name = cr.UniqueName;
    break;
  }
  case LF_ENUM: {
    EnumRecord er;
    llvm::cantFail(TypeDeserializer::deserializeAs<EnumRecord>(cvt, er));
    if (!er.hasUniqueName())
      return {};
    mangled_name = er.UniqueName;
    break;
  }
  case LF_MODIFIER: {
    ModifierRecord mfr;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<ModifierRecord>(cvt, mfr));
    return GetOrCreateType(PdbTypeSymId(mfr.ModifiedType, false));
  }
  default:
    return {};
  }

  if (!mangled_name.starts_with("$s"))
    return {};

  return m_swift_ts.GetTypeFromMangledTypename(ConstString(mangled_name));
}

CompilerType PdbAstBuilderSwift::GetOrCreateType(PdbTypeSymId type) {
  if (type.index.isNoneType())
    return {};

  lldb::user_id_t uid = toOpaqueUid(type);
  auto iter = m_uid_to_type.find(uid);
  if (iter != m_uid_to_type.end())
    return iter->second;

  auto *pdb = llvm::cast<SymbolFileNativePDB>(
      m_swift_ts.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  PdbTypeSymId best_type = GetBestPossibleDecl(type, index.tpi());

  CompilerType ct;
  if (best_type.index != type.index)
    ct = GetOrCreateType(best_type);
  else
    ct = CreateType(type, index.tpi());

  if (ct)
    m_uid_to_type[uid] = ct;
  return ct;
}

void PdbAstBuilderSwift::Dump(Stream &stream, llvm::StringRef filter) {
  m_swift_ts.Dump(stream.AsRawOstream(), filter);
}
