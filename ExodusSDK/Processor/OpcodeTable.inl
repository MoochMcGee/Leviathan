#include "DataConversion/DataConversion.pkg"

//----------------------------------------------------------------------------------------
//Constructors
//----------------------------------------------------------------------------------------
template<class T> OpcodeTable<T>::OpcodeTable(unsigned int aopcodeDecodeBits)
{
	opcodeDecodeBits = aopcodeDecodeBits;
	opcodeDecodeMask = (1 << opcodeDecodeBits) - 1;
}

//----------------------------------------------------------------------------------------
//Initialization functions
//----------------------------------------------------------------------------------------
template<class T> void OpcodeTable<T>::InitializeOpcodeTable()
{
	opcodeArray.assign(1 << opcodeDecodeBits, (T*)0);
}

//----------------------------------------------------------------------------------------
//Instruction functions
//----------------------------------------------------------------------------------------
template<class T> bool OpcodeTable<T>::AllocateRegionToOpcode(const T* opcode, const std::wstring& definition, const std::wstring& substitutions)
{
	size_t p1;
	size_t p2;

	//Find the next unhandled substitution character
	p1 = definition.find_first_not_of(L"01");
	if(p1 != std::wstring::npos)
	{
		p2 = definition.find_first_not_of(definition[p1], p1);
		if(p2 == std::wstring::npos)
		{
			p2 = definition.length();
		}
		std::wstring replace = definition.substr(p1, p2 - p1);

		std::vector<std::wstring> dataSet;
		if(replace[0] == '*')
		{
			//Wildcard replace
			for(unsigned int i = 0; i < ((unsigned int)1 << (unsigned int)replace.length()); ++i)
			{
				std::wstring entry;
				IntToStringBase2(i, entry, (int)replace.length(), false);
				dataSet.push_back(entry);
			}
		}
		else
		{
			//Substitution character replace
			size_t temp1;
			size_t temp2;

			temp1 = substitutions.find(replace);
			if(temp1 == std::wstring::npos)
			{
				return false;
			}
			temp1 += replace.length() + 1;
			temp2 = substitutions.find_first_of(L"\t ", temp1);
			if(temp2 == std::wstring::npos)
			{
				temp2 = substitutions.length();
			}
			std::wstring operationString = substitutions.substr(temp1, temp2 - temp1);

			std::vector<std::wstring> operations = TokenizeString(operationString, L",");

			for(unsigned int i = 0; i < operations.size(); ++i)
			{
				if(operations[i].length() > replace.length())
				{
					unsigned int start;
					StringToIntBase2(operations[i].substr(0, replace.length()), start);
					unsigned int end;
					StringToIntBase2(operations[i].substr(replace.length() + 1), end);

					for(unsigned int j = start; j <= end; ++j)
					{
						std::wstring entry;
						IntToStringBase2(j, entry, (int)replace.length(), false);
						dataSet.push_back(entry);
					}
				}
				else
				{
					dataSet.push_back(operations[i]);
				}
			}
		}

		//Make recursive call with each valid substitution
		bool finalSubstitution = false;
		if(definition.find_first_not_of(L"01", p2) == std::wstring::npos)
		{
			finalSubstitution = true;
		}
		for(unsigned int i = 0; i < dataSet.size(); ++i)
		{
			std::wstring newDefinition = definition;
			for(unsigned int j = 0; j < replace.size(); ++j)
			{
				newDefinition[p1 + j] = dataSet[i][j];
			}

			if(finalSubstitution)
			{
				unsigned int opcodeIndex;
				StringToIntBase2(newDefinition, opcodeIndex);
				if(opcodeArray[opcodeIndex] != 0)
				{
					return false;
				}
				opcodeArray[opcodeIndex] = opcode;
			}
			else
			{
				if(!AllocateRegionToOpcode(opcode, newDefinition, substitutions))
				{
					return false;
				}
			}
		}
	}
	else
	{
		//If there are no remaining substitution characters, add resolved entry
		unsigned int opcodeIndex;
		StringToIntBase2(definition, opcodeIndex);
		if(opcodeArray[opcodeIndex] != 0)
		{
			return false;
		}
		opcodeArray[opcodeIndex] = opcode;
	}

	return true;
}

//----------------------------------------------------------------------------------------
template<class T> const T* OpcodeTable<T>::GetInstruction(unsigned int opcode) const
{
	return opcodeArray[opcode & opcodeDecodeMask];
}
